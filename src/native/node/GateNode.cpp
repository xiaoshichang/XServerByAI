#include "GateNode.h"

#include "BinarySerialization.h"
#include "ClientSession.h"
#include "InnerNetwork.h"
#include "message/HeartbeatCodec.h"
#include "message/InnerClusterCodec.h"
#include "message/PacketCodec.h"
#include "message/RelayCodec.h"
#include "message/RegisterCodec.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

constexpr std::string_view kGateBuildVersion = "dev";
constexpr std::string_view kGmRemoteNodeId = "GM";
constexpr std::uint16_t kResponseFlags = static_cast<std::uint16_t>(xs::net::PacketFlag::Response);
constexpr std::uint16_t kErrorResponseFlags =
    kResponseFlags | static_cast<std::uint16_t>(xs::net::PacketFlag::Error);
constexpr std::uint32_t kDefaultHeartbeatIntervalMs = 5000U;
constexpr std::uint32_t kDefaultHeartbeatTimeoutMs = 15000U;
constexpr std::uint64_t kConversationReservationLifetimeMs = 30000U;

constexpr std::int32_t kInnerProcessTypeInvalid = 3000;
constexpr std::int32_t kInnerNetworkEndpointInvalid = 3002;
constexpr std::int32_t kInnerNodeNotRegistered = 3003;
constexpr std::int32_t kInnerChannelInvalid = 3004;
constexpr std::int32_t kInnerRequestInvalid = 3005;

std::string BuildEndpointText(const xs::core::EndpointConfig& endpoint)
{
    std::ostringstream stream;
    stream << endpoint.host << ':' << endpoint.port;
    return stream.str();
}

std::string BuildTcpEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return "tcp://" + endpoint.host + ":" + std::to_string(endpoint.port);
}

std::string NormalizeConnectorHost(std::string_view host)
{
    // Wildcard bind hosts are valid for listeners, but peers cannot connect to them directly.
    if (host == "0.0.0.0")
    {
        return "127.0.0.1";
    }

    if (host == "::" || host == "[::]")
    {
        return "[::1]";
    }

    return std::string(host);
}

std::string BuildConnectorTcpEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return "tcp://" + NormalizeConnectorHost(endpoint.host) + ":" + std::to_string(endpoint.port);
}

std::string NormalizeClientAddress(std::string_view host)
{
    std::string normalized;
    if (host.size() >= 2U && host.front() == '[' && host.back() == ']')
    {
        normalized = std::string(host.substr(1U, host.size() - 2U));
    }
    else
    {
        normalized = std::string(host);
    }

    constexpr std::string_view kIpv4MappedPrefix = "::ffff:";
    if (normalized.rfind(kIpv4MappedPrefix, 0U) == 0U)
    {
        normalized.erase(0U, kIpv4MappedPrefix.size());
    }

    return normalized;
}

bool IsWildcardBindHost(std::string_view host) noexcept
{
    return host == "0.0.0.0" || host == "::" || host == "[::]" || host == "localhost";
}

void ClearOptionalError(std::string* error_message) noexcept
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

RoutingID ToRoutingId(std::string_view text)
{
    return RoutingID(reinterpret_cast<const std::byte*>(text.data()), reinterpret_cast<const std::byte*>(text.data() + text.size()));
}

std::string RoutingIdToText(std::span<const std::byte> routing_id)
{
    return std::string(
        reinterpret_cast<const char*>(routing_id.data()),
        reinterpret_cast<const char*>(routing_id.data() + routing_id.size()));
}

bool TryReadRawPacketHeader(
    std::span<const std::byte> buffer,
    xs::net::PacketHeader* header) noexcept
{
    if (header == nullptr)
    {
        return false;
    }

    *header = {};
    if (buffer.size() < xs::net::kPacketHeaderSize)
    {
        return false;
    }

    xs::net::BinaryReader reader(buffer.first(xs::net::kPacketHeaderSize));
    xs::net::PacketHeader parsed_header{};
    if (!reader.ReadUInt32(&parsed_header.magic) ||
        !reader.ReadUInt16(&parsed_header.version) ||
        !reader.ReadUInt16(&parsed_header.flags) ||
        !reader.ReadUInt32(&parsed_header.length) ||
        !reader.ReadUInt32(&parsed_header.msg_id) ||
        !reader.ReadUInt32(&parsed_header.seq))
    {
        return false;
    }

    *header = parsed_header;
    return true;
}

bool IsHeartbeatRequestPacket(const xs::net::PacketHeader& header) noexcept
{
    return header.magic == xs::net::kPacketMagic &&
           header.version == xs::net::kPacketVersion &&
           header.msg_id == xs::net::kInnerHeartbeatMsgId;
}

std::string_view InnerErrorName(std::int32_t error_code) noexcept
{
    switch (error_code)
    {
    case kInnerProcessTypeInvalid:
        return "Inner.ProcessTypeInvalid";
    case kInnerNetworkEndpointInvalid:
        return "Inner.InnerNetworkEndpointInvalid";
    case kInnerNodeNotRegistered:
        return "Inner.NodeNotRegistered";
    case kInnerChannelInvalid:
        return "Inner.ChannelInvalid";
    case kInnerRequestInvalid:
        return "Inner.RequestInvalid";
    }

    return "Inner.Unknown";
}

std::optional<std::int32_t> MapRegisterCodecErrorToInnerError(xs::net::RegisterCodecErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case xs::net::RegisterCodecErrorCode::None:
        return std::nullopt;
    case xs::net::RegisterCodecErrorCode::InvalidProcessType:
        return kInnerProcessTypeInvalid;
    case xs::net::RegisterCodecErrorCode::InvalidInnerNetworkEndpointHost:
    case xs::net::RegisterCodecErrorCode::InvalidInnerNetworkEndpointPort:
        return kInnerNetworkEndpointInvalid;
    case xs::net::RegisterCodecErrorCode::BufferTooSmall:
    case xs::net::RegisterCodecErrorCode::LengthOverflow:
    case xs::net::RegisterCodecErrorCode::InvalidArgument:
    case xs::net::RegisterCodecErrorCode::InvalidProcessFlags:
    case xs::net::RegisterCodecErrorCode::InvalidNodeId:
    case xs::net::RegisterCodecErrorCode::InvalidHeartbeatTiming:
    case xs::net::RegisterCodecErrorCode::TooManyCapabilityTags:
    case xs::net::RegisterCodecErrorCode::TrailingBytes:
        return kInnerRequestInvalid;
    }

    return kInnerRequestInvalid;
}

xs::net::Endpoint ToNetEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return xs::net::Endpoint{
        .host = endpoint.host,
        .port = endpoint.port,
    };
}

} // namespace

GateNode::GateNode(NodeCommandLineArgs args)
    : ServerNode(std::move(args))
{
}

GateNode::~GateNode() = default;

ipc::ZmqListenerState GateNode::game_inner_listener_state() const noexcept
{
    return inner_network() != nullptr ? inner_network()->listener_state() : ipc::ZmqListenerState::Stopped;
}

ipc::ZmqConnectionState GateNode::inner_connection_state(std::string_view remote_node_id) const noexcept
{
    const InnerNetworkSession* session = remote_session(remote_node_id);
    return session != nullptr ? session->connection_state : ipc::ZmqConnectionState::Stopped;
}

ipc::ZmqConnectionState GateNode::gm_inner_connection_state() const noexcept
{
    return inner_connection_state(kGmRemoteNodeId);
}

bool GateNode::cluster_ready() const noexcept
{
    return cluster_ready_;
}

std::uint64_t GateNode::cluster_ready_epoch() const noexcept
{
    return cluster_ready_epoch_;
}

bool GateNode::client_network_running() const noexcept
{
    return client_network_ != nullptr && client_network_->running();
}

std::size_t GateNode::client_network_session_count() const noexcept
{
    return client_network_ != nullptr ? client_network_->session_count() : 0U;
}

xs::core::ProcessType GateNode::role_process_type() const noexcept
{
    return xs::core::ProcessType::Gate;
}

NodeErrorCode GateNode::OnInit()
{
    const auto* config = dynamic_cast<const xs::core::GateNodeConfig*>(&node_config());
    if (config == nullptr)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Gate node requires GateNodeConfig.");
    }

    const xs::core::EndpointConfig& gm_endpoint = cluster_config().gm.inner_network_listen_endpoint;
    if (gm_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM innerNetwork.listenEndpoint.host must not be empty.");
    }

    if (gm_endpoint.port == 0U)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM innerNetwork.listenEndpoint.port must be greater than zero.");
    }

    const xs::core::EndpointConfig& inner_endpoint = config->inner_network_listen_endpoint;
    if (inner_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Gate innerNetwork.listenEndpoint.host must not be empty.");
    }

    if (inner_endpoint.port == 0U)
    {
        return SetError(
            NodeErrorCode::ConfigLoadFailed,
            "Gate innerNetwork.listenEndpoint.port must be greater than zero.");
    }

    const xs::core::EndpointConfig& auth_endpoint = config->auth_network_listen_endpoint;
    if (auth_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Gate authNetwork.listenEndpoint.host must not be empty.");
    }

    if (auth_endpoint.port == 0U)
    {
        return SetError(
            NodeErrorCode::ConfigLoadFailed,
            "Gate authNetwork.listenEndpoint.port must be greater than zero.");
    }

    const xs::core::EndpointConfig& client_endpoint = config->client_network_listen_endpoint;
    if (client_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Gate clientNetwork.listenEndpoint.host must not be empty.");
    }

    if (client_endpoint.port == 0U)
    {
        return SetError(
            NodeErrorCode::ConfigLoadFailed,
            "Gate clientNetwork.listenEndpoint.port must be greater than zero.");
    }

    runtime_state_ = RuntimeState{};
    runtime_state_.started_at_unix_ms = CurrentUnixTimeMilliseconds();
    cluster_ready_epoch_ = 0U;
    last_cluster_ready_server_now_unix_ms_ = 0U;
    next_client_conversation_ = 1U;
    cluster_ready_ = false;
    client_conversation_reservations_.clear();

    inner_network_remote_sessions().Clear();
    const InnerNetworkSessionManagerErrorCode session_result =
        inner_network_remote_sessions().Register(
            {
                .process_type = xs::core::ProcessType::Gm,
                .node_id = std::string(kGmRemoteNodeId),
                .inner_network_endpoint = ToNetEndpoint(gm_endpoint),
            });
    if (session_result != InnerNetworkSessionManagerErrorCode::None)
    {
        return SetError(
            NodeErrorCode::NodeInitFailed,
            std::string(InnerNetworkSessionManagerErrorMessage(session_result)));
    }

    for (const auto& [game_node_id, game_config] : cluster_config().games)
    {
        if (game_config.inner_network_listen_endpoint.host.empty())
        {
            return SetError(
                NodeErrorCode::ConfigLoadFailed,
                "Game innerNetwork.listenEndpoint.host must not be empty for " + game_node_id + '.');
        }

        if (game_config.inner_network_listen_endpoint.port == 0U)
        {
            return SetError(
                NodeErrorCode::ConfigLoadFailed,
                "Game innerNetwork.listenEndpoint.port must be greater than zero for " + game_node_id + '.');
        }

        const InnerNetworkSessionManagerErrorCode game_session_result =
            inner_network_remote_sessions().Register(
                {
                    .process_type = xs::core::ProcessType::Game,
                    .node_id = game_node_id,
                    .inner_network_endpoint = ToNetEndpoint(game_config.inner_network_listen_endpoint),
                    .routing_id = ToRoutingId(game_node_id),
                });
        if (game_session_result != InnerNetworkSessionManagerErrorCode::None)
        {
            return SetError(
                NodeErrorCode::NodeInitFailed,
                std::string(InnerNetworkSessionManagerErrorMessage(game_session_result)));
        }
    }

    InnerNetworkOptions inner_options;
    inner_options.local_endpoint = BuildTcpEndpoint(inner_endpoint);
    inner_options.connectors.push_back(
        {
            .id = std::string(kGmRemoteNodeId),
            .remote_endpoint = BuildConnectorTcpEndpoint(gm_endpoint),
            .routing_id = std::string(node_id()),
        });

    ClientNetworkOptions client_options;
    client_options.owner_node_id = std::string(node_id());
    client_options.listen_endpoint = BuildEndpointText(client_endpoint);
    client_options.kcp = cluster_config().kcp;
    client_options.session_admission_handler =
        [this](std::uint32_t conversation, const xs::net::Endpoint& remote_endpoint, std::string* error_message) {
            return CanOpenClientSession(conversation, remote_endpoint, error_message);
        };
    client_options.session_opened_handler = [this](ClientSession& session, std::string* error_message) {
        return HandleClientSessionOpened(session, error_message);
    };
    client_network_ = std::make_unique<ClientNetwork>(event_loop(), logger(), std::move(client_options));

    GateAuthHttpServiceOptions auth_options;
    auth_options.listen_endpoint = auth_endpoint;
    auth_options.node_id = std::string(node_id());
    auth_options.login_handler = [this](const GateAuthLoginRequest& request) {
        return HandleAuthLogin(request);
    };
    auth_http_service_ = std::make_unique<GateAuthHttpService>(event_loop(), logger(), std::move(auth_options));

    const NodeErrorCode inner_result = InitInnerNetwork(std::move(inner_options));
    if (inner_result != NodeErrorCode::None)
    {
        auth_http_service_.reset();
        client_network_.reset();
        inner_network_remote_sessions().Clear();
        return inner_result;
    }

    inner_network()->SetListenerMessageHandler([this](std::vector<std::byte> routing_id, std::vector<std::byte> payload) {
        HandleGameMessage(routing_id, payload);
    });
    inner_network()->SetConnectorStateHandler([this](std::string_view remote_node_id, ipc::ZmqConnectionState state) {
        HandleConnectorStateChanged(remote_node_id, state);
    });
    inner_network()->SetConnectorMessageHandler([this](std::string_view remote_node_id, std::vector<std::byte> payload) {
        HandleConnectorMessage(remote_node_id, payload);
    });

    const NodeErrorCode client_result = client_network_->Init();
    if (client_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(client_network_->last_error_message());
        (void)UninitInnerNetwork();
        auth_http_service_.reset();
        client_network_.reset();
        inner_network_remote_sessions().Clear();
        return SetError(client_result, error_message);
    }

    const NodeErrorCode auth_result = auth_http_service_->Init();
    if (auth_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(auth_http_service_->last_error_message());
        (void)client_network_->Uninit();
        (void)UninitInnerNetwork();
        auth_http_service_.reset();
        client_network_.reset();
        inner_network_remote_sessions().Clear();
        return SetError(auth_result, error_message);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"kcpMtu", std::to_string(cluster_config().kcp.mtu)},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", std::string(kGateBuildVersion)},
        xs::core::LogContextField{"connectorCount", ToString(inner_network()->connector_count())},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "Gate node configured runtime skeleton.", context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GateNode::OnRun()
{
    if (inner_network() == nullptr || client_network_ == nullptr || auth_http_service_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Gate node must be initialized before Run().");
    }

    const NodeErrorCode inner_result = RunInnerNetwork();
    if (inner_result != NodeErrorCode::None)
    {
        return inner_result;
    }

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{
            "gmInnerState",
            std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state())),
        },
        xs::core::LogContextField{
            "gameInnerListenerState",
            std::string(ipc::ZmqListenerStateName(game_inner_listener_state())),
        },
        xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
        xs::core::LogContextField{"clientNetworkRunning", client_network_->running() ? "true" : "false"},
        xs::core::LogContextField{"authNetworkRunning", auth_http_service_->running() ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "Gate node entered runtime state.", context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GateNode::OnUninit()
{
    if (InnerNetworkSession* session = gm_session(); session != nullptr)
    {
        session->connection_state = ipc::ZmqConnectionState::Stopped;
    }

    ResetGameSessionStates();
    ResetGmSessionState();
    ResetClusterReadyState();

    if (auth_http_service_ != nullptr)
    {
        const NodeErrorCode result = auth_http_service_->Uninit();
        const std::string error_message = std::string(auth_http_service_->last_error_message());
        auth_http_service_.reset();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }

    if (client_network_ != nullptr)
    {
        const NodeErrorCode result = client_network_->Uninit();
        const std::string error_message = std::string(client_network_->last_error_message());
        client_network_.reset();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }

    if (inner_network() != nullptr)
    {
        const NodeErrorCode result = UninitInnerNetwork();
        if (result != NodeErrorCode::None)
        {
            return result;
        }
    }

    ClearError();
    return NodeErrorCode::None;
}

void GateNode::HandleConnectorStateChanged(std::string_view remote_node_id, ipc::ZmqConnectionState state)
{
    if (remote_node_id != kGmRemoteNodeId)
    {
        return;
    }

    HandleGmConnectionStateChanged(state);
}

void GateNode::HandleGmConnectionStateChanged(ipc::ZmqConnectionState state)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    session->connection_state = state;

    const std::array<xs::core::LogContextField, 3> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerState", std::string(ipc::ZmqConnectionStateName(state))},
        xs::core::LogContextField{"registered", session->registered ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node observed GM inner connection state change.", context);

    if (state != ipc::ZmqConnectionState::Connected)
    {
        ResetGmSessionState();
        return;
    }

    if (!session->registered && !session->register_in_flight)
    {
        (void)SendRegisterRequest();
    }
}

void GateNode::HandleConnectorMessage(std::string_view remote_node_id, std::span<const std::byte> payload)
{
    if (remote_node_id != kGmRemoteNodeId)
    {
        return;
    }

    HandleGmMessage(payload);
}

void GateNode::HandleGmMessage(std::span<const std::byte> payload)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            xs::core::LogContextField{
                "gmInnerState",
                std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state())),
            },
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
        };
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored malformed GM inner packet.", context);
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerClusterReadyNotifyMsgId)
    {
        HandleClusterReadyNotify(packet);
        return;
    }

    const auto log_invalid_response_envelope = [&](std::string_view protocol_error, std::string_view log_message) {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
            xs::core::LogContextField{
                "gmInnerState",
                std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state())),
            },
        };
        session->last_protocol_error = std::string(protocol_error);
        logger().Log(xs::core::LogLevel::Warn, "inner", log_message, context);
    };

    if (packet.header.seq == xs::net::kPacketSeqNone)
    {
        log_invalid_response_envelope(
            "GM response envelope is invalid.",
            "Gate node ignored GM response with an invalid envelope.");
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
        {
            log_invalid_response_envelope(
                "GM response envelope is invalid.",
                "Gate node ignored GM response with an invalid envelope.");
            return;
        }

        HandleRegisterResponse(packet);
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
    {
        if (packet.header.flags != kResponseFlags)
        {
            log_invalid_response_envelope(
                "GM heartbeat response envelope is invalid.",
                "Gate node ignored GM heartbeat response with an invalid envelope.");
            return;
        }

        HandleHeartbeatResponse(packet);
        return;
    }

    if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
    {
        log_invalid_response_envelope(
            "GM response envelope is invalid.",
            "Gate node ignored GM response with an invalid envelope.");
        return;
    }

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node ignored an unsupported GM response packet.", context);
}

void GateNode::HandleGameMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    xs::net::PacketHeader raw_header{};
    if (!TryReadRawPacketHeader(payload, &raw_header))
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored a Game payload without a complete packet header.", context);
        return;
    }

    if (raw_header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        HandleGameRegisterMessage(routing_id, payload);
        return;
    }

    if (raw_header.msg_id == xs::net::kInnerHeartbeatMsgId)
    {
        HandleGameHeartbeatMessage(routing_id, payload);
        return;
    }

    if (raw_header.msg_id == xs::net::kRelayForwardStubCallMsgId)
    {
        HandleGameForwardStubCallMessage(routing_id, payload);
        return;
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
        xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
        xs::core::LogContextField{"msgId", std::to_string(raw_header.msg_id)},
        xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node ignored an unsupported Game inner packet.", context);
}

void GateNode::HandleGameForwardStubCallMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node dropped malformed forwarded stub call packet.", context);
        return;
    }

    if (packet.header.flags != 0U || packet.header.seq != xs::net::kPacketSeqNone)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored forwarded stub call with an invalid envelope.", context);
        return;
    }

    xs::net::RelayForwardStubCall relay_message{};
    const xs::net::RelayCodecErrorCode decode_result =
        xs::net::DecodeRelayForwardStubCall(packet.payload, &relay_message);
    if (decode_result != xs::net::RelayCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"relayError", std::string(xs::net::RelayCodecErrorMessage(decode_result))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to decode forwarded stub call payload.", context);
        return;
    }

    InnerNetworkSession* source_session = inner_network_remote_sessions().FindMutableByRoutingId(routing_id);
    if (source_session == nullptr || !source_session->registered)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", relay_message.source_game_node_id},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"targetGameNodeId", relay_message.target_game_node_id},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node rejected forwarded stub call from an unregistered Game.", context);
        return;
    }

    if (source_session->node_id != relay_message.source_game_node_id)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", source_session->node_id},
            xs::core::LogContextField{"declaredSourceGameNodeId", relay_message.source_game_node_id},
            xs::core::LogContextField{"targetGameNodeId", relay_message.target_game_node_id},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node rejected forwarded stub call with a mismatched source Game node.", context);
        return;
    }

    InnerNetworkSession* target_session = remote_session(relay_message.target_game_node_id);
    if (target_session == nullptr ||
        target_session->process_type != xs::core::ProcessType::Game ||
        target_session->connection_state != ipc::ZmqConnectionState::Connected ||
        !target_session->registered ||
        !target_session->inner_network_ready ||
        target_session->routing_id.empty())
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"sourceGameNodeId", relay_message.source_game_node_id},
            xs::core::LogContextField{"targetGameNodeId", relay_message.target_game_node_id},
            xs::core::LogContextField{"targetStubType", relay_message.target_stub_type},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node cannot forward stub call because target Game is unavailable.", context);
        return;
    }

    if (inner_network() == nullptr)
    {
        return;
    }

    const NodeErrorCode send_result = inner_network()->Send(target_session->routing_id, payload);
    if (send_result != NodeErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"sourceGameNodeId", relay_message.source_game_node_id},
            xs::core::LogContextField{"targetGameNodeId", relay_message.target_game_node_id},
            xs::core::LogContextField{"targetStubType", relay_message.target_stub_type},
            xs::core::LogContextField{"innerNetworkError", std::string(inner_network()->last_error_message())},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to forward stub call to target Game.", context);
    }
}

void GateNode::HandleGameRegisterMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node dropped malformed Game register packet.", context);
        return;
    }

    if (packet.header.flags != 0U || packet.header.seq == xs::net::kPacketSeqNone)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored invalid Game register packet envelope.", context);
        return;
    }

    auto send_response = [this, routing_id, &packet](
                             std::uint16_t flags,
                             std::span<const std::byte> response_payload) -> bool {
        const xs::net::PacketHeader response_header = xs::net::MakePacketHeader(
            xs::net::kInnerRegisterMsgId,
            packet.header.seq,
            flags,
            static_cast<std::uint32_t>(response_payload.size()));
        std::vector<std::byte> response_buffer(xs::net::kPacketHeaderSize + response_payload.size());
        const xs::net::PacketCodecErrorCode encode_result =
            xs::net::EncodePacket(response_header, response_payload, response_buffer);
        if (encode_result != xs::net::PacketCodecErrorCode::None)
        {
            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(encode_result))},
            };
            logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to encode Game register response packet.", context);
            return false;
        }

        if (inner_network() == nullptr)
        {
            return false;
        }

        const NodeErrorCode send_result = inner_network()->Send(routing_id, response_buffer);
        if (send_result != NodeErrorCode::None)
        {
            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"innerNetworkError", std::string(inner_network()->last_error_message())},
            };
            logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to send Game register response packet.", context);
            return false;
        }

        return true;
    };

    auto send_error_response = [this, &send_response, routing_id, &packet](
                                   std::int32_t error_code,
                                   std::string_view game_node_id) {
        const xs::net::RegisterErrorResponse response{
            .error_code = error_code,
            .retry_after_ms = 0U,
        };

        std::array<std::byte, xs::net::kRegisterErrorResponseSize> payload_buffer{};
        const xs::net::RegisterCodecErrorCode encode_result =
            xs::net::EncodeRegisterErrorResponse(response, payload_buffer);
        if (encode_result != xs::net::RegisterCodecErrorCode::None)
        {
            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"gameNodeId", std::string(game_node_id)},
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(encode_result))},
            };
            logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to encode Game register error response.", context);
            return;
        }

        if (!send_response(kErrorResponseFlags, payload_buffer))
        {
            return;
        }

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", std::string(game_node_id)},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Gate node rejected Game register request.",
            context,
            error_code,
            InnerErrorName(error_code));
    };

    xs::net::RegisterRequest request{};
    const xs::net::RegisterCodecErrorCode decode_result = xs::net::DecodeRegisterRequest(packet.payload, &request);
    if (decode_result != xs::net::RegisterCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(decode_result))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to decode Game register request payload.", context);

        if (const std::optional<std::int32_t> error_code = MapRegisterCodecErrorToInnerError(decode_result);
            error_code.has_value())
        {
            send_error_response(*error_code, RoutingIdToText(routing_id));
        }

        return;
    }

    if (request.process_type != static_cast<std::uint16_t>(xs::net::InnerProcessType::Game))
    {
        send_error_response(kInnerProcessTypeInvalid, request.node_id);
        return;
    }

    InnerNetworkSession* session = remote_session(request.node_id);
    if (session == nullptr || session->process_type != xs::core::ProcessType::Game)
    {
        send_error_response(kInnerNodeNotRegistered, request.node_id);
        return;
    }

    const RoutingID current_routing_id(routing_id.begin(), routing_id.end());
    if (session->routing_id != current_routing_id)
    {
        send_error_response(kInnerChannelInvalid, request.node_id);
        return;
    }

    const std::uint64_t server_now_unix_ms = CurrentUnixTimeMilliseconds();
    session->pid = request.pid;
    session->started_at_unix_ms = request.started_at_unix_ms;
    session->inner_network_endpoint = request.inner_network_endpoint;
    session->build_version = request.build_version;
    session->capability_tags = request.capability_tags;
    session->load = request.load;
    session->routing_id = current_routing_id;
    session->last_heartbeat_at_unix_ms = server_now_unix_ms;
    session->inner_network_ready = false;
    session->connection_state = ipc::ZmqConnectionState::Connected;
    session->heartbeat_timed_out = false;
    session->registered = true;
    session->heartbeat_interval_ms = kDefaultHeartbeatIntervalMs;
    session->heartbeat_timeout_ms = kDefaultHeartbeatTimeoutMs;
    session->last_server_now_unix_ms = server_now_unix_ms;
    session->last_protocol_error.clear();

    const xs::net::RegisterSuccessResponse response{
        .heartbeat_interval_ms = kDefaultHeartbeatIntervalMs,
        .heartbeat_timeout_ms = kDefaultHeartbeatTimeoutMs,
        .server_now_unix_ms = server_now_unix_ms,
    };
    std::array<std::byte, xs::net::kRegisterSuccessResponseSize> payload_buffer{};
    const xs::net::RegisterCodecErrorCode encode_result =
        xs::net::EncodeRegisterSuccessResponse(response, payload_buffer);
    if (encode_result != xs::net::RegisterCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", request.node_id},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(encode_result))},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to encode Game register success response.", context);
        return;
    }

    if (!send_response(kResponseFlags, payload_buffer))
    {
        return;
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gameNodeId", request.node_id},
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(kDefaultHeartbeatIntervalMs)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(kDefaultHeartbeatTimeoutMs)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node accepted Game register request.", context);
}

void GateNode::HandleGameHeartbeatMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    if (routing_id.empty())
    {
        return;
    }

    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();

    xs::net::PacketHeader raw_header{};
    if (!TryReadRawPacketHeader(payload, &raw_header))
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored a Game heartbeat payload without a complete packet header.", context);
        return;
    }

    if (!IsHeartbeatRequestPacket(raw_header))
    {
        return;
    }

    if (!xs::net::IsValidPacketFlags(raw_header.flags) ||
        raw_header.flags != 0U ||
        raw_header.seq == xs::net::kPacketSeqNone)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"seq", std::to_string(raw_header.seq)},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored an invalid Game heartbeat request.", context);
        return;
    }

    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode decode_packet_result = xs::net::DecodePacket(payload, &packet);
    if (decode_packet_result != xs::net::PacketCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"seq", std::to_string(raw_header.seq)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(decode_packet_result))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored a malformed Game heartbeat packet.", context);
        return;
    }

    xs::net::HeartbeatRequest request{};
    const xs::net::HeartbeatCodecErrorCode decode_request_result =
        xs::net::DecodeHeartbeatRequest(packet.payload, &request);
    if (decode_request_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"codecError", std::string(xs::net::HeartbeatCodecErrorMessage(decode_request_result))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored a malformed Game heartbeat payload.", context);
        return;
    }

    InnerNetworkSession* session = inner_network_remote_sessions().FindMutableByRoutingId(routing_id);
    if (session == nullptr || session->process_type != xs::core::ProcessType::Game || !session->registered)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", RoutingIdToText(routing_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored heartbeat from an unregistered Game inner channel.", context);
        return;
    }

    session->routing_id = RoutingID(routing_id.begin(), routing_id.end());
    session->load = request.load;
    session->last_heartbeat_at_unix_ms = now_unix_ms;
    session->inner_network_ready = true;
    session->heartbeat_timed_out = false;
    session->registered = true;
    session->connection_state = ipc::ZmqConnectionState::Connected;
    session->heartbeat_interval_ms = kDefaultHeartbeatIntervalMs;
    session->heartbeat_timeout_ms = kDefaultHeartbeatTimeoutMs;
    session->last_server_now_unix_ms = now_unix_ms;
    session->last_protocol_error.clear();

    const xs::net::HeartbeatSuccessResponse response{
        .heartbeat_interval_ms = kDefaultHeartbeatIntervalMs,
        .heartbeat_timeout_ms = kDefaultHeartbeatTimeoutMs,
        .server_now_unix_ms = now_unix_ms,
    };
    std::array<std::byte, xs::net::kHeartbeatSuccessResponseSize> response_body{};
    const xs::net::HeartbeatCodecErrorCode encode_result =
        xs::net::EncodeHeartbeatSuccessResponse(response, response_body);
    if (encode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", session->node_id},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"codecError", std::string(xs::net::HeartbeatCodecErrorMessage(encode_result))},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to encode Game heartbeat success response.", context);
        return;
    }

    const xs::net::PacketHeader response_header = xs::net::MakePacketHeader(
        xs::net::kInnerHeartbeatMsgId,
        packet.header.seq,
        kResponseFlags,
        static_cast<std::uint32_t>(response_body.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kHeartbeatSuccessResponseSize> response_packet{};
    const xs::net::PacketCodecErrorCode packet_encode_result =
        xs::net::EncodePacket(response_header, response_body, response_packet);
    if (packet_encode_result != xs::net::PacketCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gameNodeId", session->node_id},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_encode_result))},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to wrap Game heartbeat success response.", context);
        return;
    }

    if (inner_network() == nullptr)
    {
        return;
    }

    const NodeErrorCode send_result = inner_network()->Send(routing_id, response_packet);
    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gameNodeId", session->node_id},
        xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"loadScore", ToString(static_cast<std::uint64_t>(request.load.load_score))},
    };
    if (send_result != NodeErrorCode::None)
    {
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to send Game heartbeat success response.", context);
        return;
    }

    logger().Log(xs::core::LogLevel::Debug, "inner", "Gate node refreshed Game heartbeat state.", context);
}

void GateNode::HandleClusterReadyNotify(const xs::net::PacketView& packet)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    if (packet.header.flags != 0U || packet.header.seq != xs::net::kPacketSeqNone)
    {
        session->last_protocol_error = "GM clusterReady notify envelope is invalid.";
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored GM cluster ready notify with an invalid envelope.");
        return;
    }

    if (!session->registered)
    {
        session->last_protocol_error = "GM clusterReady notify arrived before Gate registration completed.";
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored GM cluster ready notify before registration completed.");
        return;
    }

    xs::net::ClusterReadyNotify notify{};
    const xs::net::InnerClusterCodecErrorCode decode_result = xs::net::DecodeClusterReadyNotify(packet.payload, &notify);
    if (decode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to decode GM cluster ready notify.");
        return;
    }

    if (notify.ready_epoch < cluster_ready_epoch_)
    {
        logger().Log(xs::core::LogLevel::Info, "inner", "Gate node ignored stale GM cluster ready notify.");
        return;
    }

    if (!notify.cluster_ready)
    {
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored GM cluster ready=false notify in M4-02 scope.");
        return;
    }

    if (client_network_ != nullptr)
    {
        const NodeErrorCode client_result = client_network_->Run();
        if (client_result != NodeErrorCode::None)
        {
            session->last_protocol_error = std::string(client_network_->last_error_message());
            const std::array<xs::core::LogContextField, 1> context{
                xs::core::LogContextField{"clientNetworkError", session->last_protocol_error},
            };
            logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to apply GM cluster ready notify to client network.", context);
            return;
        }
    }

    if (auth_http_service_ != nullptr)
    {
        const NodeErrorCode auth_result = auth_http_service_->Run();
        if (auth_result != NodeErrorCode::None)
        {
            session->last_protocol_error = std::string(auth_http_service_->last_error_message());
            if (client_network_ != nullptr)
            {
                (void)client_network_->Stop();
            }
            const std::array<xs::core::LogContextField, 1> context{
                xs::core::LogContextField{"authNetworkError", session->last_protocol_error},
            };
            logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to apply GM cluster ready notify to auth network.", context);
            return;
        }
    }

    logger().Log(xs::core::LogLevel::Info, "inner", "Gate client admission endpoints opened.");

    cluster_ready_epoch_ = notify.ready_epoch;
    cluster_ready_ = true;
    last_cluster_ready_server_now_unix_ms_ = notify.server_now_unix_ms;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
        xs::core::LogContextField{"clusterReady", "true"},
        xs::core::LogContextField{"statusFlags", std::to_string(notify.status_flags)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(notify.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"clientNetworkRunning", client_network_running() ? "true" : "false"},
        xs::core::LogContextField{"authNetworkRunning", auth_http_service_ != nullptr && auth_http_service_->running() ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node accepted GM cluster ready notify.", context);
}

void GateNode::HandleRegisterResponse(const xs::net::PacketView& packet)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    if (session->register_seq == 0U || packet.header.seq != session->register_seq)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(session->register_seq)},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Gate node ignored a stale GM register response.", context);
        return;
    }

    session->register_in_flight = false;
    session->register_seq = 0U;

    if (packet.header.flags == kErrorResponseFlags)
    {
        xs::net::RegisterErrorResponse response{};
        const xs::net::RegisterCodecErrorCode decode_result =
            xs::net::DecodeRegisterErrorResponse(packet.payload, &response);
        if (decode_result != xs::net::RegisterCodecErrorCode::None)
        {
            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
                xs::core::LogContextField{
                    "codecError",
                    std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
                },
            };
            session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to decode GM register error response.", context);
            return;
        }

        session->registered = false;
        session->last_protocol_error =
            "GM rejected register request with error code " + std::to_string(response.error_code) + ".";

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"errorCode", std::to_string(response.error_code)},
            xs::core::LogContextField{"retryAfterMs", std::to_string(response.retry_after_ms)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Gate node received GM register error response.",
            context);
        return;
    }

    xs::net::RegisterSuccessResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::RegisterCodecErrorCode::None)
    {
    session->registered = false;
    session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));

        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
            },
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to decode GM register success response.", context);
        return;
    }

    session->registered = true;
    session->heartbeat_interval_ms = response.heartbeat_interval_ms;
    session->heartbeat_timeout_ms = response.heartbeat_timeout_ms;
    session->last_server_now_unix_ms = response.server_now_unix_ms;
    session->last_heartbeat_at_unix_ms = CurrentUnixTimeMilliseconds();
    session->last_protocol_error.clear();
    session->heartbeat_seq = 0U;

    StartOrResetHeartbeatTimer(response.heartbeat_interval_ms);

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node accepted GM register success response.", context);
}

void GateNode::HandleHeartbeatResponse(const xs::net::PacketView& packet)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    if (session->heartbeat_seq == 0U || packet.header.seq != session->heartbeat_seq)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(session->heartbeat_seq)},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Gate node ignored a stale GM heartbeat response.", context);
        return;
    }

    session->heartbeat_seq = 0U;

    xs::net::HeartbeatSuccessResponse response{};
    const xs::net::HeartbeatCodecErrorCode decode_result =
        xs::net::DecodeHeartbeatSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::HeartbeatCodecErrorMessage(decode_result)),
            },
        };
        session->last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to decode GM heartbeat success response.", context);
        return;
    }

    const bool heartbeat_config_changed =
        session->heartbeat_interval_ms != response.heartbeat_interval_ms ||
        session->heartbeat_timeout_ms != response.heartbeat_timeout_ms ||
        session->heartbeat_timer_id == 0;

    session->heartbeat_interval_ms = response.heartbeat_interval_ms;
    session->heartbeat_timeout_ms = response.heartbeat_timeout_ms;
    session->last_server_now_unix_ms = response.server_now_unix_ms;
    session->last_heartbeat_at_unix_ms = CurrentUnixTimeMilliseconds();
    session->last_protocol_error.clear();

    if (heartbeat_config_changed)
    {
        StartOrResetHeartbeatTimer(response.heartbeat_interval_ms);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
    };
    logger().Log(xs::core::LogLevel::Debug, "inner", "Gate node accepted GM heartbeat success response.", context);
}

bool GateNode::SendRegisterRequest()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr ||
        session->connection_state != ipc::ZmqConnectionState::Connected ||
        session->register_in_flight)
    {
        return false;
    }

    const xs::core::GateNodeConfig* config = gate_config();
    if (config == nullptr)
    {
        session->last_protocol_error = "Gate node configuration is unavailable.";
        return false;
    }

    const xs::net::RegisterRequest request{
        .process_type = static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate),
        .process_flags = 0U,
        .node_id = std::string(node_id()),
        .pid = pid(),
        .started_at_unix_ms = runtime_state_.started_at_unix_ms,
        .inner_network_endpoint = ToNetEndpoint(config->inner_network_listen_endpoint),
        .build_version = std::string(kGateBuildVersion),
        .capability_tags = {},
        .load = xs::net::LoadSnapshot{},
    };

    std::size_t payload_size = 0U;
    const xs::net::RegisterCodecErrorCode size_result =
        xs::net::GetRegisterRequestWireSize(request, &payload_size);
    if (size_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(size_result));
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(size_result))},
            xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to size GM register request.", context);
        return false;
    }

    std::vector<std::byte> payload(payload_size);
    const xs::net::RegisterCodecErrorCode encode_result = xs::net::EncodeRegisterRequest(request, payload);
    if (encode_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(encode_result));
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(encode_result))},
            xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to encode GM register request.", context);
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence();
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerRegisterMsgId,
        seq,
        0U,
        static_cast<std::uint32_t>(payload.size()));

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + payload.size());
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
            xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to wrap GM register request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
            xs::core::LogContextField{"innerNetworkError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to send GM register request.", context);
        return false;
    }

    session->register_in_flight = true;
    session->register_seq = seq;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", std::string(kGateBuildVersion)},
        xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node sent GM register request.", context);
    return true;
}

bool GateNode::SendHeartbeatRequest()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr || !session->registered ||
        session->connection_state != ipc::ZmqConnectionState::Connected ||
        session->heartbeat_seq != 0U)
    {
        return false;
    }

    const xs::net::HeartbeatRequest request{
        .sent_at_unix_ms = CurrentUnixTimeMilliseconds(),
        .status_flags = 0U,
        .load = xs::net::LoadSnapshot{},
    };

    std::array<std::byte, xs::net::kHeartbeatRequestSize> payload{};
    const xs::net::HeartbeatCodecErrorCode encode_result = xs::net::EncodeHeartbeatRequest(request, payload);
    if (encode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(encode_result));
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"codecError", std::string(xs::net::HeartbeatCodecErrorMessage(encode_result))},
            xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to encode GM heartbeat request.", context);
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence();
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerHeartbeatMsgId,
        seq,
        0U,
        static_cast<std::uint32_t>(payload.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kHeartbeatRequestSize> packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
            xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to wrap GM heartbeat request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
            xs::core::LogContextField{"innerNetworkError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to send GM heartbeat request.", context);
        return false;
    }

    session->heartbeat_seq = seq;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"sentAtUnixMs", ToString(request.sent_at_unix_ms)},
        xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Debug, "inner", "Gate node sent GM heartbeat request.", context);
    return true;
}

void GateNode::ResetGmSessionState()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    CancelHeartbeatTimer();
    session->registered = false;
    session->register_in_flight = false;
    session->register_seq = 0U;
    session->heartbeat_seq = 0U;
    session->heartbeat_interval_ms = 0U;
    session->heartbeat_timeout_ms = 0U;
    session->last_server_now_unix_ms = 0U;
    session->last_heartbeat_at_unix_ms = 0U;
}

void GateNode::ResetGameSessionStates()
{
    const std::vector<InnerNetworkSession> snapshot = inner_network_remote_sessions().Snapshot();
    for (const InnerNetworkSession& entry : snapshot)
    {
        if (entry.process_type != xs::core::ProcessType::Game)
        {
            continue;
        }

        InnerNetworkSession* session = remote_session(entry.node_id);
        if (session == nullptr)
        {
            continue;
        }

        session->pid = 0U;
        session->started_at_unix_ms = 0U;
        session->build_version.clear();
        session->capability_tags.clear();
        session->load = xs::net::LoadSnapshot{};
        session->last_heartbeat_at_unix_ms = 0U;
        session->inner_network_ready = false;
        session->connection_state = ipc::ZmqConnectionState::Stopped;
        session->register_seq = 0U;
        session->heartbeat_seq = 0U;
        session->heartbeat_interval_ms = 0U;
        session->heartbeat_timeout_ms = 0U;
        session->last_server_now_unix_ms = 0U;
        session->last_protocol_error.clear();
        session->heartbeat_timed_out = false;
        session->registered = false;
        session->register_in_flight = false;
    }
}

void GateNode::ResetClusterReadyState()
{
    cluster_ready_epoch_ = 0U;
    last_cluster_ready_server_now_unix_ms_ = 0U;
    next_client_conversation_ = 1U;
    cluster_ready_ = false;
    client_conversation_reservations_.clear();
}

void GateNode::StartOrResetHeartbeatTimer(std::uint32_t interval_ms)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    CancelHeartbeatTimer();

    const xs::core::TimerCreateResult timer_result =
        event_loop().timers().CreateRepeating(std::chrono::milliseconds(interval_ms), [this]() {
            (void)SendHeartbeatRequest();
        });
    if (!xs::core::IsTimerID(timer_result))
    {
        session->last_protocol_error =
            "Failed to create GM heartbeat timer: " +
            std::string(xs::core::TimerErrorMessage(xs::core::TimerErrorFromCreateResult(timer_result)));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
            xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(session->heartbeat_timeout_ms)},
            xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to schedule GM heartbeat timer.", context);
        return;
    }

    session->heartbeat_timer_id = timer_result;

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(session->heartbeat_timeout_ms)},
        xs::core::LogContextField{"clusterReady", cluster_ready_ ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Debug, "inner", "Gate node scheduled GM heartbeat timer.", context);
}

void GateNode::CancelHeartbeatTimer() noexcept
{
    InnerNetworkSession* session = gm_session();
    if (session != nullptr && session->heartbeat_timer_id > 0)
    {
        (void)event_loop().timers().Cancel(session->heartbeat_timer_id);
        session->heartbeat_timer_id = 0;
    }
}

std::uint32_t GateNode::ConsumeNextInnerSequence() noexcept
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return 1U;
    }

    std::uint32_t seq = session->next_seq;
    if (seq == xs::net::kPacketSeqNone)
    {
        seq = 1U;
    }

    session->next_seq = seq + 1U;
    if (session->next_seq == xs::net::kPacketSeqNone)
    {
        session->next_seq = 1U;
    }

    return seq;
}

std::uint32_t GateNode::ConsumeNextClientConversation() noexcept
{
    std::uint32_t candidate = next_client_conversation_ == 0U ? 1U : next_client_conversation_;
    const std::uint32_t start = candidate;
    while (candidate == 0U ||
           client_conversation_reservations_.contains(candidate) ||
           (client_network_ != nullptr && client_network_->FindSessionByConversation(candidate) != nullptr))
    {
        ++candidate;
        if (candidate == 0U)
        {
            candidate = 1U;
        }

        if (candidate == start)
        {
            return 0U;
        }
    }

    next_client_conversation_ = candidate + 1U;
    if (next_client_conversation_ == 0U)
    {
        next_client_conversation_ = 1U;
    }

    return candidate;
}

GateAuthLoginResult GateNode::HandleAuthLogin(const GateAuthLoginRequest& request)
{
    GateAuthLoginResult result;
    if (!cluster_ready_ || client_network_ == nullptr || !client_network_->running())
    {
        result.status_code = 503;
        result.error = "Gate client admission is not ready.";
        return result;
    }

    const xs::core::GateNodeConfig* config = gate_config();
    if (config == nullptr)
    {
        result.status_code = 500;
        result.error = "Gate node configuration is unavailable.";
        return result;
    }

    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();
    PruneExpiredClientConversationReservations(now_unix_ms);

    const std::uint32_t conversation = ConsumeNextClientConversation();
    if (conversation == 0U)
    {
        result.status_code = 503;
        result.error = "Gate has no available client conversations.";
        return result;
    }

    const std::string remote_address = NormalizeClientAddress(request.remote_address);
    const std::uint64_t expires_at_unix_ms = now_unix_ms + kConversationReservationLifetimeMs;
    client_conversation_reservations_[conversation] = ClientConversationReservation{
        .account = request.account,
        .remote_address = remote_address,
        .issued_at_unix_ms = now_unix_ms,
        .expires_at_unix_ms = expires_at_unix_ms,
    };

    result.success = true;
    result.status_code = 200;
    result.gate_node_id = std::string(node_id());
    result.kcp_host = ResolveAdvertisedClientHost(remote_address);
    result.kcp_port = config->client_network_listen_endpoint.port;
    result.conversation = conversation;
    result.issued_at_unix_ms = now_unix_ms;
    result.expires_at_unix_ms = expires_at_unix_ms;

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"account", request.account},
        xs::core::LogContextField{"conversation", std::to_string(conversation)},
        xs::core::LogContextField{"remoteAddress", remote_address},
        xs::core::LogContextField{"expiresAtUnixMs", ToString(expires_at_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "auth.http", "Gate issued a client conversation reservation.", context);
    return result;
}

bool GateNode::CanOpenClientSession(
    std::uint32_t conversation,
    const xs::net::Endpoint& remote_endpoint,
    std::string* error_message)
{
    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();
    PruneExpiredClientConversationReservations(now_unix_ms);

    const auto iterator = client_conversation_reservations_.find(conversation);
    if (iterator == client_conversation_reservations_.end())
    {
        if (error_message != nullptr)
        {
            *error_message = "Conversation was not issued by Gate HTTP login.";
        }
        return false;
    }

    if (iterator->second.expires_at_unix_ms != 0U && iterator->second.expires_at_unix_ms <= now_unix_ms)
    {
        client_conversation_reservations_.erase(iterator);
        if (error_message != nullptr)
        {
            *error_message = "Conversation reservation expired before the KCP session opened.";
        }
        return false;
    }

    const std::string remote_address = NormalizeClientAddress(remote_endpoint.host);
    if (!iterator->second.remote_address.empty() && remote_address != iterator->second.remote_address)
    {
        if (error_message != nullptr)
        {
            *error_message = "Conversation remote address did not match the HTTP login source.";
        }
        return false;
    }

    ClearOptionalError(error_message);
    return true;
}

bool GateNode::HandleClientSessionOpened(
    ClientSession& session,
    std::string* error_message)
{
    const auto iterator = client_conversation_reservations_.find(session.conversation());
    if (iterator == client_conversation_reservations_.end())
    {
        if (error_message != nullptr)
        {
            *error_message = "Conversation reservation was missing when the client session opened.";
        }
        return false;
    }

    const std::string remote_address = NormalizeClientAddress(session.remote_endpoint().host);
    if (!iterator->second.remote_address.empty() && remote_address != iterator->second.remote_address)
    {
        if (error_message != nullptr)
        {
            *error_message = "Conversation remote address changed before the client session opened.";
        }
        return false;
    }

    const ClientSessionErrorCode activate_result = session.Activate(CurrentUnixTimeMilliseconds());
    if (activate_result != ClientSessionErrorCode::None)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string(session.last_error_message());
        }
        return false;
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"account", iterator->second.account},
        xs::core::LogContextField{"sessionId", ToString(session.session_id())},
        xs::core::LogContextField{"conversation", std::to_string(session.conversation())},
        xs::core::LogContextField{"remoteEndpoint", session.remote_endpoint().host + ':' + std::to_string(session.remote_endpoint().port)},
    };
    logger().Log(xs::core::LogLevel::Info, "auth.http", "Gate admitted an authenticated client session.", context);

    client_conversation_reservations_.erase(iterator);
    ClearOptionalError(error_message);
    return true;
}

void GateNode::PruneExpiredClientConversationReservations(std::uint64_t now_unix_ms) noexcept
{
    for (auto iterator = client_conversation_reservations_.begin(); iterator != client_conversation_reservations_.end();)
    {
        if (iterator->second.expires_at_unix_ms != 0U && iterator->second.expires_at_unix_ms <= now_unix_ms)
        {
            iterator = client_conversation_reservations_.erase(iterator);
            continue;
        }

        ++iterator;
    }
}

std::string GateNode::ResolveAdvertisedClientHost(std::string_view remote_address) const
{
    const xs::core::GateNodeConfig* config = gate_config();
    if (config == nullptr)
    {
        return std::string(remote_address);
    }

    if (IsWildcardBindHost(config->client_network_listen_endpoint.host))
    {
        return remote_address.empty()
            ? NormalizeConnectorHost(config->client_network_listen_endpoint.host)
            : std::string(remote_address);
    }

    return NormalizeClientAddress(config->client_network_listen_endpoint.host);
}

const xs::core::GateNodeConfig* GateNode::gate_config() const noexcept
{
    return dynamic_cast<const xs::core::GateNodeConfig*>(&node_config());
}

InnerNetworkSession* GateNode::remote_session(std::string_view remote_node_id) noexcept
{
    return inner_network_remote_sessions().FindMutableByNodeId(remote_node_id);
}

const InnerNetworkSession* GateNode::remote_session(std::string_view remote_node_id) const noexcept
{
    return inner_network_remote_sessions().FindByNodeId(remote_node_id);
}

InnerNetworkSession* GateNode::gm_session() noexcept
{
    return remote_session(kGmRemoteNodeId);
}

const InnerNetworkSession* GateNode::gm_session() const noexcept
{
    return remote_session(kGmRemoteNodeId);
}

} // namespace xs::node

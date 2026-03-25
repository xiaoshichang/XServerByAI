#include "GateNode.h"

#include "InnerNetwork.h"
#include "TimeUtils.h"
#include "message/HeartbeatCodec.h"
#include "message/InnerClusterCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

std::string RoutingIdToText(std::span<const std::byte> routing_id)
{
    return std::string(
        reinterpret_cast<const char*>(routing_id.data()),
        reinterpret_cast<const char*>(routing_id.data() + routing_id.size()));
}

std::uint64_t CurrentUnixTimeMillisecondsValue() noexcept
{
    const std::int64_t now_unix_ms = xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow());
    return now_unix_ms > 0 ? static_cast<std::uint64_t>(now_unix_ms) : 0U;
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
    cluster_ready_ = false;

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

    InnerNetworkOptions inner_options;
    inner_options.local_endpoint = BuildTcpEndpoint(inner_endpoint);
    inner_options.connectors.push_back(
        {
            .id = std::string(kGmRemoteNodeId),
            .remote_endpoint = BuildTcpEndpoint(gm_endpoint),
            .routing_id = std::string(node_id()),
        });

    ClientNetworkOptions client_options;
    client_options.listen_endpoint = BuildEndpointText(client_endpoint);
    client_options.kcp = cluster_config().kcp;
    client_network_ = std::make_unique<ClientNetwork>(event_loop(), logger(), std::move(client_options));

    const NodeErrorCode inner_result = InitInnerNetwork(std::move(inner_options));
    if (inner_result != NodeErrorCode::None)
    {
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
        client_network_.reset();
        inner_network_remote_sessions().Clear();
        return SetError(client_result, error_message);
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
    if (inner_network() == nullptr || client_network_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Gate node must be initialized before Run().");
    }

    const NodeErrorCode inner_result = RunInnerNetwork();
    if (inner_result != NodeErrorCode::None)
    {
        return inner_result;
    }

    const std::array<xs::core::LogContextField, 5> context{
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

    ResetGmSessionState();

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

    if ((packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags) ||
        packet.header.seq == xs::net::kPacketSeqNone)
    {
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
        session->last_protocol_error = "GM response envelope is invalid.";
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored GM response with an invalid envelope.", context);
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        HandleRegisterResponse(packet);
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
    {
        HandleHeartbeatResponse(packet);
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
    const std::string remote_node_id = RoutingIdToText(routing_id);
    InnerNetworkSession* session = remote_session(remote_node_id);
    if (session != nullptr)
    {
        session->routing_id = RoutingID(routing_id.begin(), routing_id.end());
        session->connection_state = ipc::ZmqConnectionState::Connected;
    }

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gameNodeId", remote_node_id},
        xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
        xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node received an unsupported Game inner packet.", context);
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
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
        };
        session->last_protocol_error = "GM clusterReady notify envelope is invalid.";
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored GM cluster ready notify with an invalid envelope.", context);
        return;
    }

    if (!session->registered)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"registered", session->registered ? "true" : "false"},
        };
        session->last_protocol_error = "GM clusterReady notify arrived before Gate registration completed.";
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node ignored GM cluster ready notify before registration completed.", context);
        return;
    }

    xs::net::ClusterReadyNotify notify{};
    const xs::net::InnerClusterCodecErrorCode decode_result =
        xs::net::DecodeClusterReadyNotify(packet.payload, &notify);
    if (decode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::InnerClusterCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"nodeId", std::string(node_id())},
        };
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to decode GM cluster ready notify.", context);
        return;
    }

    if (notify.ready_epoch < cluster_ready_epoch_)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
            xs::core::LogContextField{"currentReadyEpoch", ToString(cluster_ready_epoch_)},
            xs::core::LogContextField{"clusterReady", notify.cluster_ready ? "true" : "false"},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"clientNetworkRunning", client_network_running() ? "true" : "false"},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Gate node ignored stale GM cluster ready notify.", context);
        return;
    }

    cluster_ready_epoch_ = notify.ready_epoch;
    cluster_ready_ = notify.cluster_ready;
    last_cluster_ready_server_now_unix_ms_ = notify.server_now_unix_ms;
    session->last_protocol_error.clear();

    if (client_network_ != nullptr)
    {
        const NodeErrorCode client_result = cluster_ready_ ? client_network_->Run() : client_network_->Stop();
        if (client_result != NodeErrorCode::None)
        {
            session->last_protocol_error = std::string(client_network_->last_error_message());

            const std::array<xs::core::LogContextField, 5> context{
                xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
                xs::core::LogContextField{"clusterReady", notify.cluster_ready ? "true" : "false"},
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"clientNetworkRunning", client_network_->running() ? "true" : "false"},
                xs::core::LogContextField{"clientNetworkError", session->last_protocol_error},
            };
            logger().Log(xs::core::LogLevel::Error, "inner", "Gate node failed to apply GM cluster ready notify to client network.", context);
            return;
        }
    }

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
        xs::core::LogContextField{"clusterReady", notify.cluster_ready ? "true" : "false"},
        xs::core::LogContextField{"statusFlags", std::to_string(notify.status_flags)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(notify.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"clientNetworkRunning", client_network_running() ? "true" : "false"},
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

    if (packet.header.flags == kErrorResponseFlags)
    {
        xs::net::HeartbeatErrorResponse response{};
        const xs::net::HeartbeatCodecErrorCode decode_result =
            xs::net::DecodeHeartbeatErrorResponse(packet.payload, &response);
        if (decode_result != xs::net::HeartbeatCodecErrorCode::None)
        {
            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
                xs::core::LogContextField{
                    "codecError",
                    std::string(xs::net::HeartbeatCodecErrorMessage(decode_result)),
                },
            };
            session->last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Gate node failed to decode GM heartbeat error response.", context);
            return;
        }

        session->last_protocol_error =
            "GM rejected heartbeat with error code " + std::to_string(response.error_code) + ".";

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"errorCode", std::to_string(response.error_code)},
            xs::core::LogContextField{"retryAfterMs", std::to_string(response.retry_after_ms)},
            xs::core::LogContextField{"requireFullRegister", response.require_full_register ? "true" : "false"},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Gate node received GM heartbeat error response.",
            context);

        if (response.require_full_register)
        {
            ResetGmSessionState();
            if (session->connection_state == ipc::ZmqConnectionState::Connected)
            {
                (void)SendRegisterRequest();
            }
        }

        return;
    }

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
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node accepted GM heartbeat success response.", context);
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
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node sent GM heartbeat request.", context);
    return true;
}

void GateNode::ResetGmSessionState()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        ResetClusterReadyState();
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
    ResetClusterReadyState();
}

void GateNode::ResetClusterReadyState()
{
    cluster_ready_epoch_ = 0U;
    last_cluster_ready_server_now_unix_ms_ = 0U;
    cluster_ready_ = false;

    if (client_network_ != nullptr && client_network_->running())
    {
        const NodeErrorCode result = client_network_->Stop();
        if (result != NodeErrorCode::None)
        {
            InnerNetworkSession* session = gm_session();
            if (session != nullptr)
            {
                session->last_protocol_error = std::string(client_network_->last_error_message());
            }
        }
    }
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
    logger().Log(xs::core::LogLevel::Info, "inner", "Gate node scheduled GM heartbeat timer.", context);
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

std::uint64_t GateNode::CurrentUnixTimeMilliseconds() const noexcept
{
    return CurrentUnixTimeMillisecondsValue();
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

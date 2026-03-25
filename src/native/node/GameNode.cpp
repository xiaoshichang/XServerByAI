#include "GameNode.h"

#include "InnerNetwork.h"
#include "TimeUtils.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

constexpr std::string_view kGameBuildVersion = "dev";
constexpr std::string_view kGmRemoteNodeId = "GM";
constexpr std::uint16_t kResponseFlags = static_cast<std::uint16_t>(xs::net::PacketFlag::Response);
constexpr std::uint16_t kErrorResponseFlags =
    kResponseFlags | static_cast<std::uint16_t>(xs::net::PacketFlag::Error);

std::string BuildTcpEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return "tcp://" + endpoint.host + ":" + std::to_string(endpoint.port);
}

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
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

GameNode::GameNode(NodeCommandLineArgs args)
    : ServerNode(std::move(args))
{
}

GameNode::~GameNode() = default;

std::string_view GameNode::gm_inner_remote_endpoint() const noexcept
{
    return gm_inner_remote_endpoint_;
}

std::string_view GameNode::configured_inner_endpoint() const noexcept
{
    return configured_inner_endpoint_;
}

std::string_view GameNode::managed_assembly_name() const noexcept
{
    return runtime_state_.managed_assembly_name;
}

ipc::ZmqConnectionState GameNode::inner_connection_state(std::string_view remote_node_id) const noexcept
{
    const InnerNetworkSession* session = remote_session(remote_node_id);
    return session != nullptr ? session->connection_state : ipc::ZmqConnectionState::Stopped;
}

ipc::ZmqConnectionState GameNode::gm_inner_connection_state() const noexcept
{
    return inner_connection_state(kGmRemoteNodeId);
}

xs::core::ProcessType GameNode::role_process_type() const noexcept
{
    return xs::core::ProcessType::Game;
}

NodeErrorCode GameNode::OnInit()
{
    const auto* config = dynamic_cast<const xs::core::GameNodeConfig*>(&node_config());
    if (config == nullptr)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Game node requires GameNodeConfig.");
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
        return SetError(NodeErrorCode::ConfigLoadFailed, "Game innerNetwork.listenEndpoint.host must not be empty.");
    }

    if (inner_endpoint.port == 0U)
    {
        return SetError(
            NodeErrorCode::ConfigLoadFailed,
            "Game innerNetwork.listenEndpoint.port must be greater than zero.");
    }

    gm_inner_remote_endpoint_ = BuildTcpEndpoint(gm_endpoint);
    configured_inner_endpoint_ = BuildTcpEndpoint(inner_endpoint);
    configured_inner_endpoint_config_ = inner_endpoint;
    gm_inner_connection_state_cache_ = ipc::ZmqConnectionState::Stopped;
    runtime_state_ = RuntimeState{};
    runtime_state_.build_version = std::string(kGameBuildVersion);
    runtime_state_.managed_assembly_name = config->managed.assembly_name;
    runtime_state_.started_at_unix_ms = CurrentUnixTimeMilliseconds();
    inner_network_remote_sessions().Clear();

    InnerNetworkOptions inner_options;
    inner_options.connectors.push_back(
        {
            .id = std::string(kGmRemoteNodeId),
            .remote_endpoint = gm_inner_remote_endpoint_,
            .routing_id = std::string(node_id()),
        });

    const auto register_remote_session =
        [this](xs::core::ProcessType process_type, std::string_view remote_node_id, const xs::core::EndpointConfig& endpoint) {
            const InnerNetworkSessionManagerErrorCode session_result =
                inner_network_remote_sessions().Register(
                    {
                        .process_type = process_type,
                        .node_id = std::string(remote_node_id),
                        .inner_network_endpoint = ToNetEndpoint(endpoint),
                    });
            if (session_result != InnerNetworkSessionManagerErrorCode::None)
            {
                return SetError(
                    NodeErrorCode::NodeInitFailed,
                    std::string(InnerNetworkSessionManagerErrorMessage(session_result)));
            }

            return NodeErrorCode::None;
        };

    if (register_remote_session(xs::core::ProcessType::Gm, kGmRemoteNodeId, gm_endpoint) != NodeErrorCode::None)
    {
        return NodeErrorCode::NodeInitFailed;
    }

    for (const auto& [gate_node_id, gate_config] : cluster_config().gates)
    {
        if (gate_config.inner_network_listen_endpoint.host.empty())
        {
            return SetError(
                NodeErrorCode::ConfigLoadFailed,
                "Gate innerNetwork.listenEndpoint.host must not be empty for " + gate_node_id + '.');
        }

        if (gate_config.inner_network_listen_endpoint.port == 0U)
        {
            return SetError(
                NodeErrorCode::ConfigLoadFailed,
                "Gate innerNetwork.listenEndpoint.port must be greater than zero for " + gate_node_id + '.');
        }

        const NodeErrorCode session_result = register_remote_session(
            xs::core::ProcessType::Gate,
            gate_node_id,
            gate_config.inner_network_listen_endpoint);
        if (session_result != NodeErrorCode::None)
        {
            return session_result;
        }

        inner_options.connectors.push_back(
            {
                .id = gate_node_id,
                .remote_endpoint = BuildTcpEndpoint(gate_config.inner_network_listen_endpoint),
                .routing_id = std::string(node_id()),
            });
    }

    const NodeErrorCode init_result = InitInnerNetwork(std::move(inner_options));
    if (init_result != NodeErrorCode::None)
    {
        gm_inner_remote_endpoint_.clear();
        configured_inner_endpoint_.clear();
        configured_inner_endpoint_config_ = xs::core::EndpointConfig{};
        gm_inner_connection_state_cache_ = ipc::ZmqConnectionState::Stopped;
        inner_network_remote_sessions().Clear();
        ResetRuntimeState();
        ResetGmSessionState();
        ResetGateSessionStates();
        return init_result;
    }

    inner_network()->SetConnectorStateHandler([this](std::string_view remote_node_id, ipc::ZmqConnectionState state) {
        HandleConnectorStateChanged(remote_node_id, state);
    });
    inner_network()->SetConnectorMessageHandler([this](std::string_view remote_node_id, std::vector<std::byte> payload) {
        HandleConnectorMessage(remote_node_id, payload);
    });

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", runtime_state_.build_version},
        xs::core::LogContextField{"connectorCount", ToString(inner_network()->connector_count())},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "Game node configured runtime skeleton.", context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GameNode::OnRun()
{
    if (inner_network() == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Game node must be initialized before Run().");
    }

    const NodeErrorCode inner_result = RunInnerNetwork();
    if (inner_result != NodeErrorCode::None)
    {
        return inner_result;
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{
            "gmInnerState",
            std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state())),
        },
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "Game node entered runtime state.", context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GameNode::OnUninit()
{
    if (InnerNetworkSession* session = gm_session(); session != nullptr)
    {
        session->connection_state = ipc::ZmqConnectionState::Stopped;
    }

    ResetGateSessionStates();
    ResetGmSessionState();
    gm_inner_connection_state_cache_ = ipc::ZmqConnectionState::Stopped;

    if (inner_network() != nullptr)
    {
        const NodeErrorCode result = UninitInnerNetwork();
        gm_inner_remote_endpoint_.clear();
        configured_inner_endpoint_.clear();
        configured_inner_endpoint_config_ = xs::core::EndpointConfig{};
        ResetRuntimeState();
        if (result != NodeErrorCode::None)
        {
            return result;
        }
    }
    else
    {
        gm_inner_remote_endpoint_.clear();
        configured_inner_endpoint_.clear();
        configured_inner_endpoint_config_ = xs::core::EndpointConfig{};
        ResetRuntimeState();
    }

    ClearError();
    return NodeErrorCode::None;
}

void GameNode::HandleConnectorStateChanged(std::string_view remote_node_id, ipc::ZmqConnectionState state)
{
    if (remote_node_id == kGmRemoteNodeId)
    {
        HandleGmConnectionStateChanged(state);
        return;
    }

    HandleGateConnectionStateChanged(remote_node_id, state);
}

void GameNode::HandleGmConnectionStateChanged(ipc::ZmqConnectionState state)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    gm_inner_connection_state_cache_ = state;
    session->connection_state = state;

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerState", std::string(ipc::ZmqConnectionStateName(state))},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        xs::core::LogContextField{"registered", session->registered ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node observed GM inner connection state change.", context);

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

void GameNode::HandleGateConnectionStateChanged(std::string_view gate_node_id, ipc::ZmqConnectionState state)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session == nullptr)
    {
        return;
    }

    session->connection_state = state;
    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
        xs::core::LogContextField{"gateInnerState", std::string(ipc::ZmqConnectionStateName(state))},
        xs::core::LogContextField{"remoteEndpoint", std::string(inner_network()->remote_endpoint(gate_node_id))},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node observed Gate inner connection state change.", context);
}

void GameNode::HandleConnectorMessage(std::string_view remote_node_id, std::span<const std::byte> payload)
{
    if (remote_node_id == kGmRemoteNodeId)
    {
        HandleGmMessage(payload);
        return;
    }

    HandleGateMessage(remote_node_id, payload);
}

void GameNode::HandleGmMessage(std::span<const std::byte> payload)
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
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
            xs::core::LogContextField{
                "gmInnerState",
                std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state_cache_)),
            },
            xs::core::LogContextField{"packetError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node ignored malformed GM inner packet.", context);
        return;
    }

    if ((packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags) ||
        packet.header.seq == xs::net::kPacketSeqNone)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{
                "gmInnerState",
                std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state_cache_)),
            },
        };
        session->last_protocol_error = "GM response envelope is invalid.";
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node ignored GM response with an invalid envelope.", context);
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
        xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored an unsupported GM response packet.", context);
}

void GameNode::HandleGateMessage(std::string_view gate_node_id, std::span<const std::byte> payload)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session == nullptr)
    {
        return;
    }

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
        xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
        xs::core::LogContextField{"remoteEndpoint", std::string(inner_network()->remote_endpoint(gate_node_id))},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored an unsupported Gate response packet.", context);
}

void GameNode::HandleRegisterResponse(const xs::net::PacketView& packet)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    if (session->register_seq == 0U || packet.header.seq != session->register_seq)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(session->register_seq)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM register response.", context);
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
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
                xs::core::LogContextField{
                    "codecError",
                    std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
                },
                xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            };
            session->registered = false;
            session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM register error response.", context);
            return;
        }

        session->registered = false;
        session->last_protocol_error =
            "GM rejected register request with error code " + std::to_string(response.error_code) + ".";

        const std::array<xs::core::LogContextField, 7> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"errorCode", std::to_string(response.error_code)},
            xs::core::LogContextField{"retryAfterMs", std::to_string(response.retry_after_ms)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node received GM register error response.",
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

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM register success response.", context);
        return;
    }

    session->registered = true;
    session->heartbeat_interval_ms = response.heartbeat_interval_ms;
    session->heartbeat_timeout_ms = response.heartbeat_timeout_ms;
    session->last_server_now_unix_ms = response.server_now_unix_ms;
    session->last_heartbeat_at_unix_ms = CurrentUnixTimeMilliseconds();
    session->heartbeat_seq = 0U;
    session->last_protocol_error.clear();

    StartOrResetHeartbeatTimer(response.heartbeat_interval_ms);

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM register success response.", context);
}

void GameNode::HandleHeartbeatResponse(const xs::net::PacketView& packet)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    if (session->heartbeat_seq == 0U || packet.header.seq != session->heartbeat_seq)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(session->heartbeat_seq)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM heartbeat response.", context);
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
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
                xs::core::LogContextField{
                    "codecError",
                    std::string(xs::net::HeartbeatCodecErrorMessage(decode_result)),
                },
                xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            };
            session->last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM heartbeat error response.", context);
            return;
        }

        session->last_protocol_error =
            "GM rejected heartbeat with error code " + std::to_string(response.error_code) + ".";

        const std::array<xs::core::LogContextField, 7> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"errorCode", std::to_string(response.error_code)},
            xs::core::LogContextField{"retryAfterMs", std::to_string(response.retry_after_ms)},
            xs::core::LogContextField{"requireFullRegister", response.require_full_register ? "true" : "false"},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node received GM heartbeat error response.",
            context);

        if (response.require_full_register)
        {
            ResetGmSessionState();
            if (gm_inner_connection_state_cache_ == ipc::ZmqConnectionState::Connected)
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
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::HeartbeatCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        session->last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM heartbeat success response.", context);
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

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM heartbeat success response.", context);
}

bool GameNode::SendRegisterRequest()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr ||
        gm_inner_connection_state_cache_ != ipc::ZmqConnectionState::Connected ||
        session->register_in_flight)
    {
        return false;
    }

    const xs::net::RegisterRequest request{
        .process_type = static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
        .process_flags = 0U,
        .node_id = std::string(node_id()),
        .pid = pid(),
        .started_at_unix_ms = runtime_state_.started_at_unix_ms,
        .inner_network_endpoint = ToNetEndpoint(configured_inner_endpoint_config_),
        .build_version = runtime_state_.build_version,
        .capability_tags = runtime_state_.capability_tags,
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
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to size GM register request.", context);
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
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode GM register request.", context);
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
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap GM register request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send GM register request.", context);
        return false;
    }

    session->register_in_flight = true;
    session->register_seq = seq;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", runtime_state_.build_version},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent GM register request.", context);
    return true;
}

bool GameNode::SendHeartbeatRequest()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr || !session->registered ||
        gm_inner_connection_state_cache_ != ipc::ZmqConnectionState::Connected ||
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
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode GM heartbeat request.", context);
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
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap GM heartbeat request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send GM heartbeat request.", context);
        return false;
    }

    session->heartbeat_seq = seq;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"sentAtUnixMs", ToString(request.sent_at_unix_ms)},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent GM heartbeat request.", context);
    return true;
}

void GameNode::ResetRuntimeState() noexcept
{
    runtime_state_ = RuntimeState{};
}

void GameNode::ResetGmSessionState()
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

void GameNode::ResetGateSessionStates()
{
    const std::vector<InnerNetworkSession> snapshot = inner_network_remote_sessions().Snapshot();
    for (const InnerNetworkSession& entry : snapshot)
    {
        if (entry.process_type != xs::core::ProcessType::Gate)
        {
            continue;
        }

        InnerNetworkSession* session = remote_session(entry.node_id);
        if (session == nullptr)
        {
            continue;
        }

        session->connection_state = ipc::ZmqConnectionState::Stopped;
        session->registered = false;
        session->register_in_flight = false;
        session->register_seq = 0U;
        session->heartbeat_seq = 0U;
        session->heartbeat_interval_ms = 0U;
        session->heartbeat_timeout_ms = 0U;
        session->last_server_now_unix_ms = 0U;
        session->last_heartbeat_at_unix_ms = 0U;
    }
}

void GameNode::StartOrResetHeartbeatTimer(std::uint32_t interval_ms)
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
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
            xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(session->heartbeat_timeout_ms)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "Game node failed to schedule GM heartbeat timer.", context);
        return;
    }

    session->heartbeat_timer_id = timer_result;

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(session->heartbeat_timeout_ms)},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node scheduled GM heartbeat timer.", context);
}

void GameNode::CancelHeartbeatTimer() noexcept
{
    InnerNetworkSession* session = gm_session();
    if (session != nullptr && session->heartbeat_timer_id > 0)
    {
        (void)event_loop().timers().Cancel(session->heartbeat_timer_id);
        session->heartbeat_timer_id = 0;
    }
}

std::uint32_t GameNode::ConsumeNextInnerSequence() noexcept
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

std::uint64_t GameNode::CurrentUnixTimeMilliseconds() const noexcept
{
    return CurrentUnixTimeMillisecondsValue();
}

InnerNetworkSession* GameNode::remote_session(std::string_view remote_node_id) noexcept
{
    return inner_network_remote_sessions().FindMutableByNodeId(remote_node_id);
}

const InnerNetworkSession* GameNode::remote_session(std::string_view remote_node_id) const noexcept
{
    return inner_network_remote_sessions().FindByNodeId(remote_node_id);
}

InnerNetworkSession* GameNode::gm_session() noexcept
{
    return remote_session(kGmRemoteNodeId);
}

const InnerNetworkSession* GameNode::gm_session() const noexcept
{
    return remote_session(kGmRemoteNodeId);
}

} // namespace xs::node

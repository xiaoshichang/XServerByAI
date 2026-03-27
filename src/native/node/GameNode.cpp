#include "GameNode.h"

#include "InnerNetwork.h"
#include "message/InnerClusterCodec.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
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
constexpr std::string_view kUnknownServerEntityId = "unknown";
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

std::string GenerateGuidText()
{
    std::array<std::uint8_t, 16> bytes{};
    std::random_device random_device;
    for (std::uint8_t& value : bytes)
    {
        value = static_cast<std::uint8_t>(random_device());
    }

    // RFC 4122 variant + version 4 layout.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        if (index == 4U || index == 6U || index == 8U || index == 10U)
        {
            stream << '-';
        }

        stream << std::setw(2) << static_cast<std::uint32_t>(bytes[index]);
    }

    return stream.str();
}

std::string ResolveServerEntityId(std::string_view assigned_entity_id)
{
    if (!assigned_entity_id.empty() && assigned_entity_id != kUnknownServerEntityId)
    {
        return std::string(assigned_entity_id);
    }

    return GenerateGuidText();
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

bool GameNode::all_nodes_online() const noexcept
{
    return all_nodes_online_;
}

std::uint64_t GameNode::cluster_nodes_online_server_now_unix_ms() const noexcept
{
    return last_cluster_nodes_online_server_now_unix_ms_;
}

bool GameNode::mesh_ready() const noexcept
{
    return mesh_ready_state_.current;
}

std::uint64_t GameNode::mesh_ready_reported_at_unix_ms() const noexcept
{
    return mesh_ready_state_.last_reported_at_unix_ms;
}

std::uint64_t GameNode::assignment_epoch() const noexcept
{
    return ownership_state_.assignment_epoch;
}

std::uint64_t GameNode::ownership_server_now_unix_ms() const noexcept
{
    return ownership_state_.server_now_unix_ms;
}

std::vector<xs::net::ServerStubOwnershipEntry> GameNode::ownership_assignments() const
{
    return ownership_state_.assignments;
}

std::vector<xs::net::ServerStubOwnershipEntry> GameNode::owned_stub_assignments() const
{
    return ownership_state_.owned_assignments;
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

    runtime_state_ = RuntimeState{};
    runtime_state_.managed_assembly_name = config->managed.assembly_name;
    runtime_state_.started_at_unix_ms = CurrentUnixTimeMilliseconds();
    mesh_ready_state_ = MeshReadyState{};
    ownership_state_ = OwnershipState{};
    service_ready_state_ = ServiceReadyState{};
    last_cluster_nodes_online_server_now_unix_ms_ = 0U;
    all_nodes_online_ = false;
    inner_network_remote_sessions().Clear();

    InnerNetworkOptions inner_options;
    inner_options.connectors.push_back(
        {
            .id = std::string(kGmRemoteNodeId),
            .remote_endpoint = BuildTcpEndpoint(gm_endpoint),
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

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", std::string(kGameBuildVersion)},
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

    const std::array<std::string_view, 1> initial_connectors{kGmRemoteNodeId};
    const NodeErrorCode inner_result = inner_network()->Run(initial_connectors);
    if (inner_result != NodeErrorCode::None)
    {
        return SetError(inner_result, std::string(inner_network()->last_error_message()));
    }

    const std::array<xs::core::LogContextField, 3> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
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

    if (inner_network() != nullptr)
    {
        const NodeErrorCode result = UninitInnerNetwork();
        ResetRuntimeState();
        if (result != NodeErrorCode::None)
        {
            return result;
        }
    }
    else
    {
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

    session->connection_state = state;

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerState", std::string(ipc::ZmqConnectionStateName(state))},
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
    const std::array<xs::core::LogContextField, 3> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
        xs::core::LogContextField{"gateInnerState", std::string(ipc::ZmqConnectionStateName(state))},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node observed Gate inner connection state change.", context);

    if (state != ipc::ZmqConnectionState::Connected)
    {
        CancelGateHeartbeatTimer(gate_node_id);
        session->inner_network_ready = false;
        session->registered = false;
        session->register_in_flight = false;
        session->register_seq = 0U;
        session->heartbeat_seq = 0U;
        session->heartbeat_interval_ms = 0U;
        session->heartbeat_timeout_ms = 0U;
        session->last_server_now_unix_ms = 0U;
        session->last_heartbeat_at_unix_ms = 0U;
        RefreshMeshReadyState();
        return;
    }

    if (state == ipc::ZmqConnectionState::Connected && all_nodes_online_ &&
        !session->registered && !session->register_in_flight)
    {
        (void)SendGateRegisterRequest(gate_node_id);
    }

    RefreshMeshReadyState();
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

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            xs::core::LogContextField{
                "gmInnerState",
                std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state())),
            },
            xs::core::LogContextField{"packetError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node ignored malformed GM inner packet.", context);
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

    if (packet.header.msg_id == xs::net::kInnerClusterNodesOnlineNotifyMsgId)
    {
        HandleClusterNodesOnlineNotify(packet);
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerServerStubOwnershipSyncMsgId)
    {
        HandleServerStubOwnershipSync(packet);
        return;
    }

    if (packet.header.seq == xs::net::kPacketSeqNone)
    {
        log_invalid_response_envelope(
            "GM response envelope is invalid.",
            "Game node ignored GM response with an invalid envelope.");
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
        {
            log_invalid_response_envelope(
                "GM response envelope is invalid.",
                "Game node ignored GM response with an invalid envelope.");
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
                "Game node ignored GM heartbeat response with an invalid envelope.");
            return;
        }

        HandleHeartbeatResponse(packet);
        return;
    }

    if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
    {
        log_invalid_response_envelope(
            "GM response envelope is invalid.",
            "Game node ignored GM response with an invalid envelope.");
        return;
    }

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
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

    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            xs::core::LogContextField{"packetError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node ignored malformed Gate inner packet.", context);
        return;
    }

    const auto log_invalid_response_envelope = [&](std::string_view protocol_error, std::string_view log_message) {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
        };
        session->last_protocol_error = std::string(protocol_error);
        logger().Log(xs::core::LogLevel::Warn, "inner", log_message, context);
    };

    if (packet.header.seq == xs::net::kPacketSeqNone)
    {
        log_invalid_response_envelope(
            "Gate response envelope is invalid.",
            "Game node ignored Gate response with an invalid envelope.");
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
        {
            log_invalid_response_envelope(
                "Gate response envelope is invalid.",
                "Game node ignored Gate response with an invalid envelope.");
            return;
        }

        HandleGateRegisterResponse(gate_node_id, packet);
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
    {
        if (packet.header.flags != kResponseFlags)
        {
            log_invalid_response_envelope(
                "Gate heartbeat response envelope is invalid.",
                "Game node ignored Gate heartbeat response with an invalid envelope.");
            return;
        }

        HandleGateHeartbeatResponse(gate_node_id, packet);
        return;
    }

    if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
    {
        log_invalid_response_envelope(
            "Gate response envelope is invalid.",
            "Game node ignored Gate response with an invalid envelope.");
        return;
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
        xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored an unsupported Gate response packet.", context);
}

void GameNode::HandleClusterNodesOnlineNotify(const xs::net::PacketView& packet)
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
        session->last_protocol_error = "GM clusterNodesOnline notify envelope is invalid.";
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node ignored GM cluster nodes online notify with an invalid envelope.",
            context);
        return;
    }

    if (!session->registered)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"registered", session->registered ? "true" : "false"},
        };
        session->last_protocol_error = "GM clusterNodesOnline notify arrived before Game registration completed.";
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node ignored GM cluster nodes online notify before registration completed.",
            context);
        return;
    }

    xs::net::ClusterNodesOnlineNotify notify{};
    const xs::net::InnerClusterCodecErrorCode decode_result =
        xs::net::DecodeClusterNodesOnlineNotify(packet.payload, &notify);
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
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM cluster nodes online notify.", context);
        return;
    }

    all_nodes_online_ = notify.all_nodes_online;
    last_cluster_nodes_online_server_now_unix_ms_ = notify.server_now_unix_ms;
    session->last_protocol_error.clear();

    if (notify.all_nodes_online)
    {
        StartGateConnectors();
    }

    RefreshMeshReadyState();

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"allNodesOnline", notify.all_nodes_online ? "true" : "false"},
        xs::core::LogContextField{"statusFlags", std::to_string(notify.status_flags)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(notify.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM cluster nodes online notify.", context);
}

void GameNode::HandleServerStubOwnershipSync(const xs::net::PacketView& packet)
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
        session->last_protocol_error = "GM ownership sync envelope is invalid.";
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node ignored GM ownership sync with an invalid envelope.",
            context);
        return;
    }

    if (!session->registered)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"registered", session->registered ? "true" : "false"},
        };
        session->last_protocol_error = "GM ownership sync arrived before Game registration completed.";
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node ignored GM ownership sync before registration completed.",
            context);
        return;
    }

    if (!mesh_ready_state_.current)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"meshReady", mesh_ready_state_.current ? "true" : "false"},
        };
        session->last_protocol_error = "GM ownership sync arrived before Game mesh ready completed.";
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node ignored GM ownership sync before mesh ready completed.",
            context);
        return;
    }

    xs::net::ServerStubOwnershipSync sync{};
    const xs::net::InnerClusterCodecErrorCode decode_result = xs::net::DecodeServerStubOwnershipSync(packet.payload, &sync);
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
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM ownership sync.", context);
        return;
    }

    if (sync.assignment_epoch < ownership_state_.assignment_epoch)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"assignmentEpoch", ToString(sync.assignment_epoch)},
            xs::core::LogContextField{"currentAssignmentEpoch", ToString(ownership_state_.assignment_epoch)},
            xs::core::LogContextField{"assignmentCount", ToString(sync.assignments.size())},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM ownership sync.", context);
        return;
    }

    ApplyStubOwnership(sync);
    RefreshLocalServiceReadyState();
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"assignmentEpoch", ToString(sync.assignment_epoch)},
        xs::core::LogContextField{"assignmentCount", ToString(sync.assignments.size())},
        xs::core::LogContextField{"ownedAssignmentCount", ToString(ownership_state_.owned_assignments.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(sync.server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM ownership sync.", context);
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
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(session->register_seq)},
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
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
                xs::core::LogContextField{
                    "codecError",
                    std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
                },
            };
            session->registered = false;
            session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM register error response.", context);
            return;
        }

        session->registered = false;
        session->last_protocol_error =
            "GM rejected register request with error code " + std::to_string(response.error_code) + ".";

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"errorCode", std::to_string(response.error_code)},
            xs::core::LogContextField{"retryAfterMs", std::to_string(response.retry_after_ms)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
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
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM register success response.", context);
        return;
    }

    session->registered = true;
    session->inner_network_ready = false;
    session->heartbeat_interval_ms = response.heartbeat_interval_ms;
    session->heartbeat_timeout_ms = response.heartbeat_timeout_ms;
    session->last_server_now_unix_ms = response.server_now_unix_ms;
    session->last_heartbeat_at_unix_ms = CurrentUnixTimeMilliseconds();
    session->heartbeat_seq = 0U;
    session->last_protocol_error.clear();

    StartOrResetHeartbeatTimer(response.heartbeat_interval_ms);

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
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
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(session->heartbeat_seq)},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM heartbeat response.", context);
        return;
    }

    session->heartbeat_seq = 0U;

    xs::net::HeartbeatSuccessResponse response{};
    const xs::net::HeartbeatCodecErrorCode decode_result =
        xs::net::DecodeHeartbeatSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::HeartbeatCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
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
    session->inner_network_ready = true;
    session->last_protocol_error.clear();

    if (heartbeat_config_changed)
    {
        StartOrResetHeartbeatTimer(response.heartbeat_interval_ms);
    }

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM heartbeat success response.", context);
    RefreshMeshReadyState();
}

bool GameNode::SendRegisterRequest()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr ||
        session->connection_state != ipc::ZmqConnectionState::Connected ||
        session->register_in_flight)
    {
        return false;
    }

    const xs::core::GameNodeConfig* config = game_config();
    if (config == nullptr)
    {
        session->last_protocol_error = "Game node configuration is unavailable.";
        return false;
    }

    const xs::net::RegisterRequest request{
        .process_type = static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
        .process_flags = 0U,
        .node_id = std::string(node_id()),
        .pid = pid(),
        .started_at_unix_ms = runtime_state_.started_at_unix_ms,
        .inner_network_endpoint = ToNetEndpoint(config->inner_network_listen_endpoint),
        .build_version = std::string(kGameBuildVersion),
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
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
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
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode GM register request.", context);
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence(session);
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
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap GM register request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
            xs::core::LogContextField{"innerNetworkError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send GM register request.", context);
        return false;
    }

    session->register_in_flight = true;
    session->register_seq = seq;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", std::string(kGameBuildVersion)},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent GM register request.", context);
    return true;
}

bool GameNode::SendHeartbeatRequest()
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
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode GM heartbeat request.", context);
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence(session);
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
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap GM heartbeat request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
            xs::core::LogContextField{"innerNetworkError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send GM heartbeat request.", context);
        return false;
    }

    session->heartbeat_seq = seq;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"sentAtUnixMs", ToString(request.sent_at_unix_ms)},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent GM heartbeat request.", context);
    return true;
}

bool GameNode::SendMeshReadyReport(bool mesh_ready)
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr || !session->registered ||
        session->connection_state != ipc::ZmqConnectionState::Connected)
    {
        return false;
    }

    const xs::net::GameGateMeshReadyReport report{
        .mesh_ready = mesh_ready,
        .status_flags = 0U,
        .reported_at_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::array<std::byte, xs::net::kGameGateMeshReadyReportSize> payload{};
    const xs::net::InnerClusterCodecErrorCode encode_result =
        xs::net::EncodeGameGateMeshReadyReport(report, payload);
    if (encode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(encode_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"meshReady", mesh_ready ? "true" : "false"},
            xs::core::LogContextField{"codecError", session->last_protocol_error},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode mesh ready report.", context);
        return false;
    }

    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerGameGateMeshReadyReportMsgId,
        xs::net::kPacketSeqNone,
        0U,
        static_cast<std::uint32_t>(payload.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kGameGateMeshReadyReportSize> packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"meshReady", mesh_ready ? "true" : "false"},
            xs::core::LogContextField{"packetError", session->last_protocol_error},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap mesh ready report into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"meshReady", mesh_ready ? "true" : "false"},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
            xs::core::LogContextField{"innerNetworkError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send mesh ready report.", context);
        return false;
    }

    mesh_ready_state_.has_reported = true;
    mesh_ready_state_.last_reported = mesh_ready;
    mesh_ready_state_.last_reported_at_unix_ms = report.reported_at_unix_ms;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"meshReady", mesh_ready ? "true" : "false"},
        xs::core::LogContextField{"reportedAtUnixMs", ToString(report.reported_at_unix_ms)},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        xs::core::LogContextField{"allNodesOnline", all_nodes_online_ ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent mesh ready report.", context);
    return true;
}

bool GameNode::SendServiceReadyReport()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr || !session->registered ||
        session->connection_state != ipc::ZmqConnectionState::Connected ||
        ownership_state_.assignment_epoch == 0U ||
        ownership_state_.owned_assignments.empty() ||
        service_ready_state_.ready_entries.empty())
    {
        return false;
    }

    const xs::net::GameServiceReadyReport report{
        .assignment_epoch = ownership_state_.assignment_epoch,
        .local_ready = service_ready_state_.ready_entries.size() == ownership_state_.owned_assignments.size(),
        .status_flags = 0U,
        .entries = service_ready_state_.ready_entries,
        .reported_at_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::size_t wire_size = 0U;
    const xs::net::InnerClusterCodecErrorCode wire_size_result =
        xs::net::GetGameServiceReadyReportWireSize(report, &wire_size);
    if (wire_size_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(wire_size_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
            xs::core::LogContextField{"codecError", session->last_protocol_error},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to size service ready report.", context);
        return false;
    }

    std::vector<std::byte> payload(wire_size);
    const xs::net::InnerClusterCodecErrorCode encode_result =
        xs::net::EncodeGameServiceReadyReport(report, payload);
    if (encode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(encode_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
            xs::core::LogContextField{"codecError", session->last_protocol_error},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode service ready report.", context);
        return false;
    }

    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerGameServiceReadyReportMsgId,
        xs::net::kPacketSeqNone,
        0U,
        static_cast<std::uint32_t>(payload.size()));
    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + payload.size());
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
            xs::core::LogContextField{"packetError", session->last_protocol_error},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap service ready report into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
            xs::core::LogContextField{"innerNetworkError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send service ready report.", context);
        return false;
    }

    service_ready_state_.last_reported_assignment_epoch = report.assignment_epoch;
    service_ready_state_.last_reported_at_unix_ms = report.reported_at_unix_ms;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
        xs::core::LogContextField{"localReady", report.local_ready ? "true" : "false"},
        xs::core::LogContextField{"readyEntryCount", ToString(report.entries.size())},
        xs::core::LogContextField{"reportedAtUnixMs", ToString(report.reported_at_unix_ms)},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent service ready report.", context);
    return true;
}
void GameNode::HandleGateRegisterResponse(std::string_view gate_node_id, const xs::net::PacketView& packet)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session == nullptr)
    {
        return;
    }

    if (session->register_seq == 0U || packet.header.seq != session->register_seq)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(session->register_seq)},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale Gate register response.", context);
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
            const std::array<xs::core::LogContextField, 5> context{
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
                xs::core::LogContextField{
                    "codecError",
                    std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
                },
            };
            session->registered = false;
            session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode Gate register error response.", context);
            return;
        }

        session->registered = false;
        session->last_protocol_error =
            "Gate rejected register request with error code " + std::to_string(response.error_code) + ".";

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"errorCode", std::to_string(response.error_code)},
            xs::core::LogContextField{"retryAfterMs", std::to_string(response.retry_after_ms)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node received Gate register error response.",
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

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode Gate register success response.", context);
        return;
    }

    session->registered = true;
    session->inner_network_ready = false;
    session->heartbeat_interval_ms = response.heartbeat_interval_ms;
    session->heartbeat_timeout_ms = response.heartbeat_timeout_ms;
    session->last_server_now_unix_ms = response.server_now_unix_ms;
    session->last_heartbeat_at_unix_ms = CurrentUnixTimeMilliseconds();
    session->heartbeat_seq = 0U;
    session->last_protocol_error.clear();

    StartOrResetGateHeartbeatTimer(gate_node_id, response.heartbeat_interval_ms);

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted Gate register success response.", context);
    RefreshMeshReadyState();
}

void GameNode::HandleGateHeartbeatResponse(std::string_view gate_node_id, const xs::net::PacketView& packet)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session == nullptr)
    {
        return;
    }

    if (session->heartbeat_seq == 0U || packet.header.seq != session->heartbeat_seq)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(session->heartbeat_seq)},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale Gate heartbeat response.", context);
        return;
    }

    session->heartbeat_seq = 0U;

    xs::net::HeartbeatSuccessResponse response{};
    const xs::net::HeartbeatCodecErrorCode decode_result =
        xs::net::DecodeHeartbeatSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::HeartbeatCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        session->last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode Gate heartbeat success response.", context);
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
    session->inner_network_ready = true;
    session->last_protocol_error.clear();

    if (heartbeat_config_changed)
    {
        StartOrResetGateHeartbeatTimer(gate_node_id, response.heartbeat_interval_ms);
    }

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted Gate heartbeat success response.", context);
    RefreshMeshReadyState();
}

bool GameNode::SendGateRegisterRequest(std::string_view gate_node_id)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session == nullptr || inner_network() == nullptr ||
        session->connection_state != ipc::ZmqConnectionState::Connected ||
        session->register_in_flight)
    {
        return false;
    }

    const xs::core::GameNodeConfig* config = game_config();
    if (config == nullptr)
    {
        session->last_protocol_error = "Game node configuration is unavailable.";
        return false;
    }

    const xs::net::RegisterRequest request{
        .process_type = static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
        .process_flags = 0U,
        .node_id = std::string(node_id()),
        .pid = pid(),
        .started_at_unix_ms = runtime_state_.started_at_unix_ms,
        .inner_network_endpoint = ToNetEndpoint(config->inner_network_listen_endpoint),
        .build_version = std::string(kGameBuildVersion),
        .capability_tags = {},
        .load = xs::net::LoadSnapshot{},
    };

    std::size_t payload_size = 0U;
    const xs::net::RegisterCodecErrorCode size_result =
        xs::net::GetRegisterRequestWireSize(request, &payload_size);
    if (size_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(size_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(size_result))},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to size Gate register request.", context);
        return false;
    }

    std::vector<std::byte> payload(payload_size);
    const xs::net::RegisterCodecErrorCode encode_result = xs::net::EncodeRegisterRequest(request, payload);
    if (encode_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(encode_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(encode_result))},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode Gate register request.", context);
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence(session);
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
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap Gate register request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(gate_node_id, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
            xs::core::LogContextField{"innerNetworkError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send Gate register request.", context);
        return false;
    }

    session->register_in_flight = true;
    session->register_seq = seq;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", std::string(kGameBuildVersion)},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent Gate register request.", context);
    return true;
}

bool GameNode::SendGateHeartbeatRequest(std::string_view gate_node_id)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
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
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"codecError", std::string(xs::net::HeartbeatCodecErrorMessage(encode_result))},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode Gate heartbeat request.", context);
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence(session);
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
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap Gate heartbeat request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(gate_node_id, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
            xs::core::LogContextField{"innerNetworkError", session->last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send Gate heartbeat request.", context);
        return false;
    }

    session->heartbeat_seq = seq;
    session->last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", std::string(gate_node_id)},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"sentAtUnixMs", ToString(request.sent_at_unix_ms)},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent Gate heartbeat request.", context);
    return true;
}

void GameNode::StartGateConnectors()
{
    if (inner_network() == nullptr)
    {
        return;
    }

    for (const auto& [gate_node_id, gate_config] : cluster_config().gates)
    {
        (void)gate_config;

        const NodeErrorCode start_result = inner_network()->StartConnector(gate_node_id);
        if (start_result != NodeErrorCode::None)
        {
            InnerNetworkSession* session = remote_session(gate_node_id);
            if (session != nullptr)
            {
                session->last_protocol_error = std::string(inner_network()->last_error_message());
            }

            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"nodeId", std::string(node_id())},
                xs::core::LogContextField{"gateNodeId", gate_node_id},
                xs::core::LogContextField{"allNodesOnline", all_nodes_online_ ? "true" : "false"},
                xs::core::LogContextField{"innerNetworkError", std::string(inner_network()->last_error_message())},
            };
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to start Gate inner connector.", context);
            continue;
        }

        InnerNetworkSession* session = remote_session(gate_node_id);
        if (session != nullptr && session->connection_state == ipc::ZmqConnectionState::Connected &&
            !session->registered && !session->register_in_flight)
        {
            (void)SendGateRegisterRequest(gate_node_id);
        }
    }
}

void GameNode::ResetRuntimeState() noexcept
{
    runtime_state_ = RuntimeState{};
}

void GameNode::ResetMeshReadyState() noexcept
{
    mesh_ready_state_ = MeshReadyState{};
}

void GameNode::ResetOwnershipState()
{
    ownership_state_ = OwnershipState{};
    ResetServiceReadyState();
}

void GameNode::ResetServiceReadyState() noexcept
{
    service_ready_state_ = ServiceReadyState{};
}

void GameNode::ResetGmSessionState()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr)
    {
        return;
    }

    CancelHeartbeatTimer();
    all_nodes_online_ = false;
    last_cluster_nodes_online_server_now_unix_ms_ = 0U;
    ResetMeshReadyState();
    ResetOwnershipState();
    session->inner_network_ready = false;
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

        CancelGateHeartbeatTimer(entry.node_id);

        InnerNetworkSession* session = remote_session(entry.node_id);
        if (session == nullptr)
        {
            continue;
        }

        session->connection_state = ipc::ZmqConnectionState::Stopped;
        session->inner_network_ready = false;
        session->registered = false;
        session->register_in_flight = false;
        session->register_seq = 0U;
        session->heartbeat_seq = 0U;
        session->heartbeat_interval_ms = 0U;
        session->heartbeat_timeout_ms = 0U;
        session->last_server_now_unix_ms = 0U;
        session->last_heartbeat_at_unix_ms = 0U;
        session->last_protocol_error.clear();
    }

    RefreshMeshReadyState();
}

bool GameNode::AreAllGateSessionsMeshReady() const noexcept
{
    if (cluster_config().gates.empty())
    {
        return false;
    }

    for (const auto& [gate_node_id, gate_config] : cluster_config().gates)
    {
        (void)gate_config;

        const InnerNetworkSession* session = remote_session(gate_node_id);
        if (session == nullptr ||
            session->connection_state != ipc::ZmqConnectionState::Connected ||
            !session->registered ||
            !session->inner_network_ready)
        {
            return false;
        }
    }

    return true;
}

void GameNode::RefreshMeshReadyState()
{
    const bool next_mesh_ready = all_nodes_online_ && AreAllGateSessionsMeshReady();
    const bool state_changed = mesh_ready_state_.current != next_mesh_ready;
    mesh_ready_state_.current = next_mesh_ready;

    if (!next_mesh_ready)
    {
        ResetOwnershipState();
    }

    if (state_changed)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"meshReady", next_mesh_ready ? "true" : "false"},
            xs::core::LogContextField{"allNodesOnline", all_nodes_online_ ? "true" : "false"},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node refreshed mesh ready state.", context);
    }

    if (next_mesh_ready != mesh_ready_state_.last_reported || !mesh_ready_state_.has_reported)
    {
        (void)SendMeshReadyReport(next_mesh_ready);
    }

    if (next_mesh_ready)
    {
        RefreshLocalServiceReadyState();
    }
}

void GameNode::RefreshLocalServiceReadyState()
{
    if (!mesh_ready_state_.current || ownership_state_.assignment_epoch == 0U)
    {
        return;
    }

    if (ownership_state_.owned_assignments.empty() || service_ready_state_.ready_entries.empty())
    {
        return;
    }

    if (service_ready_state_.last_reported_assignment_epoch == ownership_state_.assignment_epoch)
    {
        return;
    }

    (void)SendServiceReadyReport();
}

void GameNode::ApplyStubOwnership(const xs::net::ServerStubOwnershipSync& sync)
{
    ownership_state_.assignment_epoch = sync.assignment_epoch;
    ownership_state_.server_now_unix_ms = sync.server_now_unix_ms;
    ownership_state_.assignments = sync.assignments;
    ownership_state_.owned_assignments.clear();
    service_ready_state_.ready_entries.clear();

    for (const xs::net::ServerStubOwnershipEntry& entry : sync.assignments)
    {
        if (entry.owner_game_node_id == node_id())
        {
            ownership_state_.owned_assignments.push_back(entry);
            const std::string entity_id = ResolveServerEntityId(entry.entity_id);
            service_ready_state_.ready_entries.push_back(
                xs::net::ServerStubReadyEntry{
                    .entity_type = entry.entity_type,
                    .entity_id = entity_id,
                    .ready = true,
                    .entry_flags = 0U,
                });
        }
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
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
            xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(session->heartbeat_timeout_ms)},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "Game node failed to schedule GM heartbeat timer.", context);
        return;
    }

    session->heartbeat_timer_id = timer_result;

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(session->heartbeat_timeout_ms)},
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

void GameNode::StartOrResetGateHeartbeatTimer(std::string_view gate_node_id, std::uint32_t interval_ms)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session == nullptr)
    {
        return;
    }

    CancelGateHeartbeatTimer(gate_node_id);

    const std::string gate_node_id_text(gate_node_id);
    const xs::core::TimerCreateResult timer_result =
        event_loop().timers().CreateRepeating(std::chrono::milliseconds(interval_ms), [this, gate_node_id_text]() {
            (void)SendGateHeartbeatRequest(gate_node_id_text);
        });
    if (!xs::core::IsTimerID(timer_result))
    {
        session->last_protocol_error =
            "Failed to create Gate heartbeat timer: " +
            std::string(xs::core::TimerErrorMessage(xs::core::TimerErrorFromCreateResult(timer_result)));
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gateNodeId", gate_node_id_text},
            xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
            xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(session->heartbeat_timeout_ms)},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "Game node failed to schedule Gate heartbeat timer.", context);
        return;
    }

    session->heartbeat_timer_id = timer_result;

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gateNodeId", gate_node_id_text},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(session->heartbeat_timeout_ms)},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node scheduled Gate heartbeat timer.", context);
}

void GameNode::CancelGateHeartbeatTimer(std::string_view gate_node_id) noexcept
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session != nullptr && session->heartbeat_timer_id > 0)
    {
        (void)event_loop().timers().Cancel(session->heartbeat_timer_id);
        session->heartbeat_timer_id = 0;
    }
}

std::uint32_t GameNode::ConsumeNextInnerSequence(InnerNetworkSession* session) noexcept
{
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

const xs::core::GameNodeConfig* GameNode::game_config() const noexcept
{
    return dynamic_cast<const xs::core::GameNodeConfig*>(&node_config());
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

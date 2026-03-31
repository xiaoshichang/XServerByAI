#include "GameNode.h"

#include "InnerNetwork.h"
#include "ManagedRuntimeHost.h"
#include "message/InnerClusterCodec.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <asio/post.hpp>

#include <array>
#include <chrono>
#include <cstring>
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
constexpr std::string_view kUnknownServerEntityId = "unknown";
constexpr std::string_view kManagedRuntimeLogCategory = "managed.runtime";
constexpr std::uint16_t kResponseFlags = static_cast<std::uint16_t>(xs::net::PacketFlag::Response);
constexpr std::uint16_t kErrorResponseFlags = kResponseFlags | static_cast<std::uint16_t>(xs::net::PacketFlag::Error);

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

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

std::string DescribeManagedHostError(xs::host::ManagedHostErrorCode code)
{
    return std::string(xs::host::ManagedHostErrorCanonicalName(code)) + ": " +
           std::string(xs::host::ManagedHostErrorMessage(code));
}

xs::core::LogLevel ToNativeLogLevel(std::uint32_t value) noexcept
{
    switch (static_cast<xs::host::ManagedLogLevel>(value))
    {
    case xs::host::ManagedLogLevel::Trace:
        return xs::core::LogLevel::Trace;
    case xs::host::ManagedLogLevel::Debug:
        return xs::core::LogLevel::Debug;
    case xs::host::ManagedLogLevel::Info:
        return xs::core::LogLevel::Info;
    case xs::host::ManagedLogLevel::Warn:
        return xs::core::LogLevel::Warn;
    case xs::host::ManagedLogLevel::Error:
        return xs::core::LogLevel::Error;
    case xs::host::ManagedLogLevel::Fatal:
        return xs::core::LogLevel::Fatal;
    }

    return xs::core::LogLevel::Info;
}

xs::host::ManagedRuntimeHostOptions BuildManagedRuntimeHostOptions(const xs::core::ManagedConfig& managed_config)
{
    return xs::host::ManagedRuntimeHostOptions{
        .runtime_config_path = managed_config.runtime_config_path,
        .assembly_path = managed_config.assembly_path,
        .discovery_assembly_paths = managed_config.search_assembly_paths,
    };
}

xs::net::Endpoint ToNetEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return xs::net::Endpoint{
        .host = endpoint.host,
        .port = endpoint.port,
    };
}

bool TryReadManagedUtf8View(const std::uint8_t* utf8_buffer, std::uint32_t utf8_length, std::string_view* output)
{
    if (output == nullptr)
    {
        return false;
    }

    if (utf8_buffer == nullptr)
    {
        if (utf8_length != 0U)
        {
            return false;
        }

        *output = std::string_view{};
        return true;
    }

    *output = std::string_view(reinterpret_cast<const char*>(utf8_buffer), static_cast<std::size_t>(utf8_length));
    return true;
}

bool TryReadManagedUtf8String(std::span<const std::uint8_t> utf8_buffer, std::uint32_t utf8_length, std::string* output)
{
    if (output == nullptr)
    {
        return false;
    }

    if (static_cast<std::size_t>(utf8_length) > utf8_buffer.size())
    {
        return false;
    }

    output->assign(reinterpret_cast<const char*>(utf8_buffer.data()), static_cast<std::size_t>(utf8_length));
    return true;
}

bool TryWriteManagedUtf8String(std::string_view value, std::span<std::uint8_t> utf8_buffer,
                               std::uint32_t* output_length)
{
    if (output_length == nullptr)
    {
        return false;
    }

    if (value.size() > utf8_buffer.size())
    {
        return false;
    }

    std::fill(utf8_buffer.begin(), utf8_buffer.end(), static_cast<std::uint8_t>(0));
    if (!value.empty())
    {
        std::memcpy(utf8_buffer.data(), value.data(), value.size());
    }

    *output_length = static_cast<std::uint32_t>(value.size());
    return true;
}

bool HaveEquivalentOwnedAssignments(const std::vector<xs::net::ServerStubOwnershipEntry>& left,
                                    const std::vector<xs::net::ServerStubOwnershipEntry>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0U; index < left.size(); ++index)
    {
        if (left[index].entity_type != right[index].entity_type || left[index].entity_id != right[index].entity_id ||
            left[index].owner_game_node_id != right[index].owner_game_node_id ||
            left[index].entry_flags != right[index].entry_flags)
        {
            return false;
        }
    }

    return true;
}

} // namespace

GameNode::GameNode(NodeCommandLineArgs args) : ServerNode(std::move(args))
{
}

GameNode::~GameNode() = default;

void GameNode::HandleManagedServerStubReadyCallback(void* context, std::uint64_t assignment_epoch,
                                                    const xs::host::ManagedServerStubReadyEntry* entry)
{
    if (context == nullptr || entry == nullptr)
    {
        return;
    }

    auto* game_node = static_cast<GameNode*>(context);
    const xs::host::ManagedServerStubReadyEntry entry_copy = *entry;
    asio::post(game_node->event_loop().executor(),
               [game_node, assignment_epoch, entry_copy]() mutable
               {
                   game_node->HandleManagedServerStubReady(assignment_epoch, entry_copy);
               });
}

void GameNode::HandleManagedLogCallback(void* context, std::uint32_t level, const std::uint8_t* category_utf8,
                                        std::uint32_t category_length, const std::uint8_t* message_utf8,
                                        std::uint32_t message_length)
{
    if (context == nullptr)
    {
        return;
    }

    auto* game_node = static_cast<GameNode*>(context);
    std::string_view category;
    std::string_view message;
    if (!TryReadManagedUtf8View(category_utf8, category_length, &category) ||
        !TryReadManagedUtf8View(message_utf8, message_length, &message))
    {
        game_node->logger().Log(xs::core::LogLevel::Warn, "runtime",
                                "Game node ignored managed log callback with an invalid payload.");
        return;
    }

    game_node->HandleManagedLog(level, category, message);
}

void GameNode::HandleManagedLog(std::uint32_t level, std::string_view category, std::string_view message)
{
    const std::string_view resolved_category = category.empty() ? kManagedRuntimeLogCategory : category;
    logger().Log(ToNativeLogLevel(level), resolved_category, message);
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
        return SetError(NodeErrorCode::ConfigLoadFailed,
                        "GM innerNetwork.listenEndpoint.port must be greater than zero.");
    }

    const xs::core::EndpointConfig& inner_endpoint = config->inner_network_listen_endpoint;
    if (inner_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Game innerNetwork.listenEndpoint.host must not be empty.");
    }

    if (inner_endpoint.port == 0U)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed,
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
    managed_exports_ = xs::host::ManagedExports{};
    managed_exports_loaded_ = false;

    const NodeErrorCode managed_runtime_result = InitializeManagedRuntime(config->managed);
    if (managed_runtime_result != NodeErrorCode::None)
    {
        ResetRuntimeState();
        ResetGmSessionState();
        ResetGateSessionStates();
        inner_network_remote_sessions().Clear();
        return managed_runtime_result;
    }

    InnerNetworkOptions inner_options;
    inner_options.connectors.push_back({
        .id = std::string(kGmRemoteNodeId),
        .remote_endpoint = BuildConnectorTcpEndpoint(gm_endpoint),
        .routing_id = std::string(node_id()),
    });

    const auto register_remote_session = [this](xs::core::ProcessType process_type, std::string_view remote_node_id,
                                                const xs::core::EndpointConfig& endpoint)
    {
        const InnerNetworkSessionManagerErrorCode session_result = inner_network_remote_sessions().Register({
            .process_type = process_type,
            .node_id = std::string(remote_node_id),
            .inner_network_endpoint = ToNetEndpoint(endpoint),
        });
        if (session_result != InnerNetworkSessionManagerErrorCode::None)
        {
            return SetError(NodeErrorCode::NodeInitFailed,
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
            return SetError(NodeErrorCode::ConfigLoadFailed,
                            "Gate innerNetwork.listenEndpoint.host must not be empty for " + gate_node_id + '.');
        }

        if (gate_config.inner_network_listen_endpoint.port == 0U)
        {
            return SetError(NodeErrorCode::ConfigLoadFailed,
                            "Gate innerNetwork.listenEndpoint.port must be greater than zero for " + gate_node_id +
                                '.');
        }

        const NodeErrorCode session_result = register_remote_session(xs::core::ProcessType::Gate, gate_node_id,
                                                                     gate_config.inner_network_listen_endpoint);
        if (session_result != NodeErrorCode::None)
        {
            return session_result;
        }

        inner_options.connectors.push_back({
            .id = gate_node_id,
            .remote_endpoint = BuildConnectorTcpEndpoint(gate_config.inner_network_listen_endpoint),
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

    inner_network()->SetConnectorStateHandler(
        [this](std::string_view remote_node_id, ipc::ZmqConnectionState state)
        {
            HandleConnectorStateChanged(remote_node_id, state);
        });
    inner_network()->SetConnectorMessageHandler(
        [this](std::string_view remote_node_id, std::vector<std::byte> payload)
        {
            HandleConnectorMessage(remote_node_id, payload);
        });

    logger().Log(xs::core::LogLevel::Info, "runtime", "Game node configured runtime skeleton.");

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

    logger().Log(xs::core::LogLevel::Info, "runtime", "Game node entered runtime state.");

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

NodeErrorCode GameNode::InitializeManagedRuntime(const xs::core::ManagedConfig& managed_config)
{
    const xs::host::ManagedHostErrorCode load_result =
        managed_runtime_host_.Load(BuildManagedRuntimeHostOptions(managed_config));
    if (load_result != xs::host::ManagedHostErrorCode::None)
    {
        return SetError(NodeErrorCode::NodeInitFailed,
                        "Failed to load Game managed runtime host: " + DescribeManagedHostError(load_result));
    }

    const xs::host::ManagedHostErrorCode bind_result = managed_runtime_host_.BindExports();
    if (bind_result != xs::host::ManagedHostErrorCode::None)
    {
        (void)managed_runtime_host_.Unload();
        return SetError(NodeErrorCode::NodeInitFailed,
                        "Failed to bind Game managed exports: " + DescribeManagedHostError(bind_result));
    }

    const xs::host::ManagedHostErrorCode exports_result = managed_runtime_host_.GetExports(managed_exports_);
    if (exports_result != xs::host::ManagedHostErrorCode::None)
    {
        managed_exports_ = xs::host::ManagedExports{};
        (void)managed_runtime_host_.Unload();
        return SetError(NodeErrorCode::NodeInitFailed,
                        "Failed to read Game managed exports: " + DescribeManagedHostError(exports_result));
    }

    const std::string node_id_text(node_id());
    const std::string config_path_text = config_path().lexically_normal().string();
    const xs::host::ManagedNativeCallbacks native_callbacks{
        .struct_size = sizeof(xs::host::ManagedNativeCallbacks),
        .reserved0 = 0U,
        .context = this,
        .on_server_stub_ready = &GameNode::HandleManagedServerStubReadyCallback,
        .on_log = &GameNode::HandleManagedLogCallback,
    };
    const xs::host::ManagedInitArgs init_args{
        .struct_size = sizeof(xs::host::ManagedInitArgs),
        .abi_version = xs::host::XS_MANAGED_ABI_VERSION,
        .process_type = static_cast<std::uint16_t>(role_process_type()),
        .reserved0 = 0U,
        .node_id_utf8 = reinterpret_cast<const std::uint8_t*>(node_id_text.data()),
        .node_id_length = static_cast<std::uint32_t>(node_id_text.size()),
        .config_path_utf8 = reinterpret_cast<const std::uint8_t*>(config_path_text.data()),
        .config_path_length = static_cast<std::uint32_t>(config_path_text.size()),
        .native_callbacks = native_callbacks,
    };

    const std::int32_t init_result = managed_exports_.init(&init_args);
    if (init_result != 0)
    {
        managed_exports_ = xs::host::ManagedExports{};
        (void)managed_runtime_host_.Unload();
        return SetError(NodeErrorCode::NodeInitFailed,
                        "Game managed runtime initialization returned error code " + std::to_string(init_result) + '.');
    }

    managed_exports_loaded_ = true;

    logger().Log(xs::core::LogLevel::Info, "runtime", "Game node loaded managed runtime host.");
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

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node observed GM inner connection state change.");

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
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node observed Gate inner connection state change.");

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

    if (state == ipc::ZmqConnectionState::Connected && all_nodes_online_ && !session->registered &&
        !session->register_in_flight)
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

        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node ignored malformed GM inner packet.");
        return;
    }

    const auto log_invalid_response_envelope = [&](std::string_view protocol_error, std::string_view log_message)
    {
        session->last_protocol_error = std::string(protocol_error);
        logger().Log(xs::core::LogLevel::Warn, "inner", log_message);
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
        log_invalid_response_envelope("GM response envelope is invalid.",
                                      "Game node ignored GM response with an invalid envelope.");
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
        {
            log_invalid_response_envelope("GM response envelope is invalid.",
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
            log_invalid_response_envelope("GM heartbeat response envelope is invalid.",
                                          "Game node ignored GM heartbeat response with an invalid envelope.");
            return;
        }

        HandleHeartbeatResponse(packet);
        return;
    }

    if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
    {
        log_invalid_response_envelope("GM response envelope is invalid.",
                                      "Game node ignored GM response with an invalid envelope.");
        return;
    }

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored an unsupported GM response packet.");
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

        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node ignored malformed Gate inner packet.");
        return;
    }

    const auto log_invalid_response_envelope = [&](std::string_view protocol_error, std::string_view log_message)
    {
        session->last_protocol_error = std::string(protocol_error);
        logger().Log(xs::core::LogLevel::Warn, "inner", log_message);
    };

    if (packet.header.seq == xs::net::kPacketSeqNone)
    {
        log_invalid_response_envelope("Gate response envelope is invalid.",
                                      "Game node ignored Gate response with an invalid envelope.");
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
        {
            log_invalid_response_envelope("Gate response envelope is invalid.",
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
            log_invalid_response_envelope("Gate heartbeat response envelope is invalid.",
                                          "Game node ignored Gate heartbeat response with an invalid envelope.");
            return;
        }

        HandleGateHeartbeatResponse(gate_node_id, packet);
        return;
    }

    if (packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags)
    {
        log_invalid_response_envelope("Gate response envelope is invalid.",
                                      "Game node ignored Gate response with an invalid envelope.");
        return;
    }

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored an unsupported Gate response packet.");
}

void GameNode::HandleManagedServerStubReady(std::uint64_t assignment_epoch, xs::host::ManagedServerStubReadyEntry entry)
{
    if (!managed_exports_loaded_ || assignment_epoch == 0U ||
        assignment_epoch != ownership_state_.assignment_epoch || entry.ready == 0U)
    {
        return;
    }

    std::string entity_type;
    std::string entity_id;
    if (!TryReadManagedUtf8String(
            std::span<const std::uint8_t>(entry.entity_type_utf8,
                                          xs::host::XS_MANAGED_SERVER_STUB_ENTITY_TYPE_MAX_UTF8_BYTES),
            entry.entity_type_length, &entity_type) ||
        !TryReadManagedUtf8String(std::span<const std::uint8_t>(
                                      entry.entity_id_utf8, xs::host::XS_MANAGED_SERVER_STUB_ENTITY_ID_MAX_UTF8_BYTES),
                                  entry.entity_id_length, &entity_id))
    {
        logger().Log(xs::core::LogLevel::Warn, "runtime", "Game node failed to decode managed ready callback entry.");
        return;
    }

    bool owned_by_current_node = false;
    for (const xs::net::ServerStubOwnershipEntry& owned_assignment : ownership_state_.owned_assignments)
    {
        if (owned_assignment.entity_type == entity_type)
        {
            owned_by_current_node = true;
            break;
        }
    }

    if (!owned_by_current_node)
    {
        return;
    }

    const xs::net::ServerStubReadyEntry ready_entry{
        .entity_type = std::move(entity_type),
        .entity_id = std::move(entity_id),
        .ready = true,
        .entry_flags = entry.entry_flags,
    };

    bool replaced_existing_entry = false;
    for (xs::net::ServerStubReadyEntry& existing_entry : service_ready_state_.ready_entries)
    {
        if (existing_entry.entity_id == ready_entry.entity_id || existing_entry.entity_type == ready_entry.entity_type)
        {
            existing_entry = ready_entry;
            replaced_existing_entry = true;
            break;
        }
    }

    if (!replaced_existing_entry)
    {
        service_ready_state_.ready_entries.push_back(ready_entry);
    }

    CheckAllLocalStubsReady();
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
        session->last_protocol_error = "GM clusterNodesOnline notify envelope is invalid.";
        logger().Log(xs::core::LogLevel::Warn, "inner",
                     "Game node ignored GM cluster nodes online notify with an invalid envelope.");
        return;
    }

    if (!session->registered)
    {
        session->last_protocol_error = "GM clusterNodesOnline notify arrived before Game registration completed.";
        logger().Log(xs::core::LogLevel::Warn, "inner",
                     "Game node ignored GM cluster nodes online notify before registration completed.");
        return;
    }

    xs::net::ClusterNodesOnlineNotify notify{};
    const xs::net::InnerClusterCodecErrorCode decode_result =
        xs::net::DecodeClusterNodesOnlineNotify(packet.payload, &notify);
    if (decode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM cluster nodes online notify.");
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

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM cluster nodes online notify.");
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
        session->last_protocol_error = "GM ownership sync envelope is invalid.";
        logger().Log(xs::core::LogLevel::Warn, "inner",
                     "Game node ignored GM ownership sync with an invalid envelope.");
        return;
    }

    if (!session->registered)
    {
        session->last_protocol_error = "GM ownership sync arrived before Game registration completed.";
        logger().Log(xs::core::LogLevel::Warn, "inner",
                     "Game node ignored GM ownership sync before registration completed.");
        return;
    }

    if (!mesh_ready_state_.current)
    {
        session->last_protocol_error = "GM ownership sync arrived before Game mesh ready completed.";
        logger().Log(xs::core::LogLevel::Warn, "inner",
                     "Game node ignored GM ownership sync before mesh ready completed.");
        return;
    }

    xs::net::ServerStubOwnershipSync sync{};
    const xs::net::InnerClusterCodecErrorCode decode_result =
        xs::net::DecodeServerStubOwnershipSync(packet.payload, &sync);
    if (decode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM ownership sync.");
        return;
    }

    if (sync.assignment_epoch < ownership_state_.assignment_epoch)
    {
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM ownership sync.");
        return;
    }

    if (!CreateAllLocalStubs(sync))
    {
        session->last_protocol_error = "Game node failed to apply managed ownership sync.";
        return;
    }

    CheckAllLocalStubsReady();
    session->last_protocol_error.clear();

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM ownership sync.");
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
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM register response.");
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
            session->registered = false;
            session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM register error response.");
            return;
        }

        session->registered = false;
        session->last_protocol_error =
            "GM rejected register request with error code " + std::to_string(response.error_code) + ".";

        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node received GM register error response.");
        return;
    }

    xs::net::RegisterSuccessResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->registered = false;
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));

        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM register success response.");
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

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM register success response.");
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
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM heartbeat response.");
        return;
    }

    session->heartbeat_seq = 0U;

    xs::net::HeartbeatSuccessResponse response{};
    const xs::net::HeartbeatCodecErrorCode decode_result =
        xs::net::DecodeHeartbeatSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM heartbeat success response.");
        return;
    }

    const bool heartbeat_config_changed = session->heartbeat_interval_ms != response.heartbeat_interval_ms ||
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

    logger().Log(xs::core::LogLevel::Debug, "inner", "Game node accepted GM heartbeat success response.");
    RefreshMeshReadyState();
}

bool GameNode::SendRegisterRequest()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr ||
        session->connection_state != ipc::ZmqConnectionState::Connected || session->register_in_flight)
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
    const xs::net::RegisterCodecErrorCode size_result = xs::net::GetRegisterRequestWireSize(request, &payload_size);
    if (size_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(size_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to size GM register request.");
        return false;
    }

    std::vector<std::byte> payload(payload_size);
    const xs::net::RegisterCodecErrorCode encode_result = xs::net::EncodeRegisterRequest(request, payload);
    if (encode_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(encode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode GM register request.");
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence(session);
    const xs::net::PacketHeader header =
        xs::net::MakePacketHeader(xs::net::kInnerRegisterMsgId, seq, 0U, static_cast<std::uint32_t>(payload.size()));

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + payload.size());
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap GM register request into a packet.");
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send GM register request.");
        return false;
    }

    session->register_in_flight = true;
    session->register_seq = seq;
    session->last_protocol_error.clear();

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent GM register request.");
    return true;
}

bool GameNode::SendHeartbeatRequest()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr || !session->registered ||
        session->connection_state != ipc::ZmqConnectionState::Connected || session->heartbeat_seq != 0U)
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
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode GM heartbeat request.");
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence(session);
    const xs::net::PacketHeader header =
        xs::net::MakePacketHeader(xs::net::kInnerHeartbeatMsgId, seq, 0U, static_cast<std::uint32_t>(payload.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kHeartbeatRequestSize> packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap GM heartbeat request into a packet.");
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send GM heartbeat request.");
        return false;
    }

    session->heartbeat_seq = seq;
    session->last_protocol_error.clear();

    logger().Log(xs::core::LogLevel::Debug, "inner", "Game node sent GM heartbeat request.");
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
    const xs::net::InnerClusterCodecErrorCode encode_result = xs::net::EncodeGameGateMeshReadyReport(report, payload);
    if (encode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(encode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode mesh ready report.");
        return false;
    }

    const xs::net::PacketHeader header =
        xs::net::MakePacketHeader(xs::net::kInnerGameGateMeshReadyReportMsgId, xs::net::kPacketSeqNone, 0U,
                                  static_cast<std::uint32_t>(payload.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kGameGateMeshReadyReportSize> packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap mesh ready report into a packet.");
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send mesh ready report.");
        return false;
    }

    mesh_ready_state_.has_reported = true;
    mesh_ready_state_.last_reported = mesh_ready;
    mesh_ready_state_.last_reported_at_unix_ms = report.reported_at_unix_ms;
    session->last_protocol_error.clear();

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent mesh ready report.");
    return true;
}

bool GameNode::SendServiceReadyReport()
{
    InnerNetworkSession* session = gm_session();
    if (session == nullptr || inner_network() == nullptr || !session->registered ||
        session->connection_state != ipc::ZmqConnectionState::Connected || ownership_state_.assignment_epoch == 0U ||
        ownership_state_.owned_assignments.empty() || service_ready_state_.ready_entries.empty())
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
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to size service ready report.");
        return false;
    }

    std::vector<std::byte> payload(wire_size);
    const xs::net::InnerClusterCodecErrorCode encode_result = xs::net::EncodeGameServiceReadyReport(report, payload);
    if (encode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::InnerClusterCodecErrorMessage(encode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode service ready report.");
        return false;
    }

    const xs::net::PacketHeader header =
        xs::net::MakePacketHeader(xs::net::kInnerGameServiceReadyReportMsgId, xs::net::kPacketSeqNone, 0U,
                                  static_cast<std::uint32_t>(payload.size()));
    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + payload.size());
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap service ready report into a packet.");
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(kGmRemoteNodeId, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send service ready report.");
        return false;
    }

    service_ready_state_.last_reported_assignment_epoch = report.assignment_epoch;
    service_ready_state_.last_reported_at_unix_ms = report.reported_at_unix_ms;
    session->last_protocol_error.clear();

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent service ready report.");
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
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale Gate register response.");
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
            session->registered = false;
            session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode Gate register error response.");
            return;
        }

        session->registered = false;
        session->last_protocol_error =
            "Gate rejected register request with error code " + std::to_string(response.error_code) + ".";

        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node received Gate register error response.");
        return;
    }

    xs::net::RegisterSuccessResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->registered = false;
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));

        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode Gate register success response.");
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

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted Gate register success response.");
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
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale Gate heartbeat response.");
        return;
    }

    session->heartbeat_seq = 0U;

    xs::net::HeartbeatSuccessResponse response{};
    const xs::net::HeartbeatCodecErrorCode decode_result =
        xs::net::DecodeHeartbeatSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode Gate heartbeat success response.");
        return;
    }

    const bool heartbeat_config_changed = session->heartbeat_interval_ms != response.heartbeat_interval_ms ||
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

    logger().Log(xs::core::LogLevel::Debug, "inner", "Game node accepted Gate heartbeat success response.");
    RefreshMeshReadyState();
}

bool GameNode::SendGateRegisterRequest(std::string_view gate_node_id)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session == nullptr || inner_network() == nullptr ||
        session->connection_state != ipc::ZmqConnectionState::Connected || session->register_in_flight)
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
    const xs::net::RegisterCodecErrorCode size_result = xs::net::GetRegisterRequestWireSize(request, &payload_size);
    if (size_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(size_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to size Gate register request.");
        return false;
    }

    std::vector<std::byte> payload(payload_size);
    const xs::net::RegisterCodecErrorCode encode_result = xs::net::EncodeRegisterRequest(request, payload);
    if (encode_result != xs::net::RegisterCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(encode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode Gate register request.");
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence(session);
    const xs::net::PacketHeader header =
        xs::net::MakePacketHeader(xs::net::kInnerRegisterMsgId, seq, 0U, static_cast<std::uint32_t>(payload.size()));

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + payload.size());
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        logger().Log(xs::core::LogLevel::Warn, "inner",
                     "Game node failed to wrap Gate register request into a packet.");
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(gate_node_id, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send Gate register request.");
        return false;
    }

    session->register_in_flight = true;
    session->register_seq = seq;
    session->last_protocol_error.clear();

    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent Gate register request.");
    return true;
}

bool GameNode::SendGateHeartbeatRequest(std::string_view gate_node_id)
{
    InnerNetworkSession* session = remote_session(gate_node_id);
    if (session == nullptr || inner_network() == nullptr || !session->registered ||
        session->connection_state != ipc::ZmqConnectionState::Connected || session->heartbeat_seq != 0U)
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
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode Gate heartbeat request.");
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence(session);
    const xs::net::PacketHeader header =
        xs::net::MakePacketHeader(xs::net::kInnerHeartbeatMsgId, seq, 0U, static_cast<std::uint32_t>(payload.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kHeartbeatRequestSize> packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        session->last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        logger().Log(xs::core::LogLevel::Warn, "inner",
                     "Game node failed to wrap Gate heartbeat request into a packet.");
        return false;
    }

    const NodeErrorCode send_result = inner_network()->SendToConnector(gate_node_id, packet);
    if (send_result != NodeErrorCode::None)
    {
        session->last_protocol_error = std::string(inner_network()->last_error_message());
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send Gate heartbeat request.");
        return false;
    }

    session->heartbeat_seq = seq;
    session->last_protocol_error.clear();

    logger().Log(xs::core::LogLevel::Debug, "inner", "Game node sent Gate heartbeat request.");
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

            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to start Gate inner connector.");
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
    managed_exports_loaded_ = false;
    managed_exports_ = xs::host::ManagedExports{};
    (void)managed_runtime_host_.Unload();
    runtime_state_ = RuntimeState{};
}

void GameNode::ResetMeshReadyState() noexcept
{
    mesh_ready_state_ = MeshReadyState{};
}

void GameNode::ResetOwnershipState()
{
    if (managed_exports_loaded_ && managed_exports_.reset_server_stub_ownership != nullptr)
    {
        const std::int32_t reset_result = managed_exports_.reset_server_stub_ownership();
        if (reset_result != 0)
        {
            logger().Log(xs::core::LogLevel::Warn, "runtime", "Game node failed to reset managed ownership state.");
        }
    }

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
        if (session == nullptr || session->connection_state != ipc::ZmqConnectionState::Connected ||
            !session->registered || !session->inner_network_ready)
        {
            return false;
        }
    }

    return true;
}

void GameNode::RefreshMeshReadyState()
{
    const bool all_gate_connected = all_nodes_online_ && AreAllGateSessionsMeshReady();
    const bool state_changed = mesh_ready_state_.current != all_gate_connected;
    mesh_ready_state_.current = all_gate_connected;

    if (!all_gate_connected)
    {
        ResetOwnershipState();
    }

    if (state_changed)
    {
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node refreshed mesh ready state.");
    }

    if (all_gate_connected != mesh_ready_state_.last_reported || !mesh_ready_state_.has_reported)
    {
        (void)SendMeshReadyReport(all_gate_connected);
    }

    if (all_gate_connected)
    {
        CheckAllLocalStubsReady();
    }
}

void GameNode::CheckAllLocalStubsReady()
{
    if (!mesh_ready_state_.current || ownership_state_.assignment_epoch == 0U)
    {
        return;
    }

    if (ownership_state_.owned_assignments.empty() ||
        service_ready_state_.ready_entries.size() != ownership_state_.owned_assignments.size())
    {
        return;
    }

    if (service_ready_state_.last_reported_assignment_epoch == ownership_state_.assignment_epoch)
    {
        return;
    }

    (void)SendServiceReadyReport();
}

bool GameNode::CreateAllLocalStubs(const xs::net::ServerStubOwnershipSync& sync)
{
    if (!managed_exports_loaded_ || managed_exports_.apply_server_stub_ownership == nullptr)
    {
        logger().Log(xs::core::LogLevel::Warn, "runtime",
                     "Game node cannot apply ownership before managed exports are ready.");
        return false;
    }

    std::vector<xs::host::ManagedServerStubOwnershipEntry> managed_assignments(sync.assignments.size());
    std::vector<xs::net::ServerStubOwnershipEntry> next_owned_assignments;
    next_owned_assignments.reserve(sync.assignments.size());
    for (std::size_t index = 0U; index < sync.assignments.size(); ++index)
    {
        const xs::net::ServerStubOwnershipEntry& assignment = sync.assignments[index];
        xs::host::ManagedServerStubOwnershipEntry& managed_assignment = managed_assignments[index];

        if (!TryWriteManagedUtf8String(
                assignment.entity_type,
                std::span<std::uint8_t>(managed_assignment.entity_type_utf8,
                                        xs::host::XS_MANAGED_SERVER_STUB_ENTITY_TYPE_MAX_UTF8_BYTES),
                &managed_assignment.entity_type_length) ||
            !TryWriteManagedUtf8String(
                assignment.entity_id,
                std::span<std::uint8_t>(managed_assignment.entity_id_utf8,
                                        xs::host::XS_MANAGED_SERVER_STUB_ENTITY_ID_MAX_UTF8_BYTES),
                &managed_assignment.entity_id_length) ||
            !TryWriteManagedUtf8String(assignment.owner_game_node_id,
                                       std::span<std::uint8_t>(managed_assignment.owner_game_node_id_utf8,
                                                               xs::host::XS_MANAGED_NODE_ID_MAX_UTF8_BYTES),
                                       &managed_assignment.owner_game_node_id_length))
        {
            logger().Log(xs::core::LogLevel::Warn, "runtime", "Game node failed to encode managed ownership assignment.");
            return false;
        }

        managed_assignment.entry_flags = assignment.entry_flags;
        if (assignment.owner_game_node_id == node_id())
        {
            next_owned_assignments.push_back(assignment);
        }
    }

    const bool preserve_ready_state =
        sync.assignment_epoch == ownership_state_.assignment_epoch &&
        HaveEquivalentOwnedAssignments(next_owned_assignments, ownership_state_.owned_assignments);

    xs::host::ManagedServerStubOwnershipSync managed_sync{};
    managed_sync.struct_size = sizeof(xs::host::ManagedServerStubOwnershipSync);
    managed_sync.status_flags = sync.status_flags;
    managed_sync.assignment_epoch = sync.assignment_epoch;
    managed_sync.server_now_unix_ms = sync.server_now_unix_ms;
    managed_sync.assignment_count = static_cast<std::uint32_t>(managed_assignments.size());
    managed_sync.assignments = managed_assignments.empty() ? nullptr : managed_assignments.data();

    const std::int32_t apply_result = managed_exports_.apply_server_stub_ownership(&managed_sync);
    if (apply_result != 0)
    {
        logger().Log(xs::core::LogLevel::Warn, "runtime", "Game node failed to apply ownership in managed runtime.");
        return false;
    }

    ownership_state_.assignment_epoch = sync.assignment_epoch;
    ownership_state_.server_now_unix_ms = sync.server_now_unix_ms;
    ownership_state_.assignments = sync.assignments;
    ownership_state_.owned_assignments = std::move(next_owned_assignments);

    if (!preserve_ready_state)
    {
        service_ready_state_ = ServiceReadyState{};
    }

    return true;
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
        event_loop().timers().CreateRepeating(std::chrono::milliseconds(interval_ms),
                                              [this]()
                                              {
                                                  (void)SendHeartbeatRequest();
                                              });
    if (!xs::core::IsTimerID(timer_result))
    {
        session->last_protocol_error =
            "Failed to create GM heartbeat timer: " +
            std::string(xs::core::TimerErrorMessage(xs::core::TimerErrorFromCreateResult(timer_result)));
        logger().Log(xs::core::LogLevel::Error, "inner", "Game node failed to schedule GM heartbeat timer.");
        return;
    }

    session->heartbeat_timer_id = timer_result;

    logger().Log(xs::core::LogLevel::Debug, "inner", "Game node scheduled GM heartbeat timer.");
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
        event_loop().timers().CreateRepeating(std::chrono::milliseconds(interval_ms),
                                              [this, gate_node_id_text]()
                                              {
                                                  (void)SendGateHeartbeatRequest(gate_node_id_text);
                                              });
    if (!xs::core::IsTimerID(timer_result))
    {
        session->last_protocol_error =
            "Failed to create Gate heartbeat timer: " +
            std::string(xs::core::TimerErrorMessage(xs::core::TimerErrorFromCreateResult(timer_result)));
        logger().Log(xs::core::LogLevel::Error, "inner", "Game node failed to schedule Gate heartbeat timer.");
        return;
    }

    session->heartbeat_timer_id = timer_result;

    logger().Log(xs::core::LogLevel::Debug, "inner", "Game node scheduled Gate heartbeat timer.");
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

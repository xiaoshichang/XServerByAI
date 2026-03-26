#include "GmNode.h"

#include "BinarySerialization.h"
#include "InnerNetwork.h"
#include "message/InnerClusterCodec.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

inline constexpr std::uint32_t kDefaultHeartbeatIntervalMs = 5000u;
inline constexpr std::uint32_t kDefaultHeartbeatTimeoutMs = 15000u;
inline constexpr auto kDefaultTimeoutScanInterval = std::chrono::milliseconds(1000);
inline constexpr std::uint64_t kServerStubOwnershipAssignmentEpoch = 1U;

inline constexpr std::int32_t kInnerProcessTypeInvalid = 3000;
inline constexpr std::int32_t kInnerNodeIdConflict = 3001;
inline constexpr std::int32_t kInnerNodeNotRegistered = 3003;
inline constexpr std::int32_t kInnerNetworkEndpointInvalid = 3002;
inline constexpr std::int32_t kInnerChannelInvalid = 3004;
inline constexpr std::int32_t kInnerRequestInvalid = 3005;

struct BootstrapStubDefinition final
{
    std::string_view entity_type;
    std::string_view entity_key;
};

constexpr std::array<BootstrapStubDefinition, 3> kBootstrapStubCatalog{
    BootstrapStubDefinition{"MatchService", "default"},
    BootstrapStubDefinition{"ChatService", "default"},
    BootstrapStubDefinition{"LeaderboardService", "default"},
};

std::string BuildTcpEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return "tcp://" + endpoint.host + ":" + std::to_string(endpoint.port);
}

std::string BuildInnerNetworkEndpointText(const xs::net::Endpoint& endpoint)
{
    return endpoint.host + ":" + std::to_string(endpoint.port);
}

std::string BuildInnerProcessTypeText(std::uint16_t process_type)
{
    switch (static_cast<xs::net::InnerProcessType>(process_type))
    {
    case xs::net::InnerProcessType::Gate:
        return "Gate";
    case xs::net::InnerProcessType::Game:
        return "Game";
    }

    return std::to_string(process_type);
}

std::string BuildPacketFlagsText(std::uint16_t flags)
{
    return std::to_string(flags);
}

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

bool HasPacketFlag(std::uint16_t flags, xs::net::PacketFlag flag) noexcept
{
    return (flags & static_cast<std::uint16_t>(flag)) != 0u;
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

std::optional<xs::core::ProcessType> ToCoreProcessType(std::uint16_t process_type) noexcept
{
    switch (static_cast<xs::net::InnerProcessType>(process_type))
    {
    case xs::net::InnerProcessType::Gate:
        return xs::core::ProcessType::Gate;
    case xs::net::InnerProcessType::Game:
        return xs::core::ProcessType::Game;
    }

    return std::nullopt;
}

std::string_view InnerErrorName(std::int32_t error_code) noexcept
{
    switch (error_code)
    {
    case kInnerProcessTypeInvalid:
        return "Inner.ProcessTypeInvalid";
    case kInnerNodeIdConflict:
        return "Inner.NodeIdConflict";
    case kInnerNetworkEndpointInvalid:
        return "Inner.InnerNetworkEndpointInvalid";
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

std::optional<std::int32_t> MapInnerNetworkSessionManagerErrorToInnerError(
    InnerNetworkSessionManagerErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case InnerNetworkSessionManagerErrorCode::None:
        return std::nullopt;
    case InnerNetworkSessionManagerErrorCode::InvalidProcessType:
        return kInnerProcessTypeInvalid;
    case InnerNetworkSessionManagerErrorCode::InvalidInnerNetworkEndpointHost:
    case InnerNetworkSessionManagerErrorCode::InvalidInnerNetworkEndpointPort:
        return kInnerNetworkEndpointInvalid;
    case InnerNetworkSessionManagerErrorCode::NodeIdConflict:
        return kInnerNodeIdConflict;
    case InnerNetworkSessionManagerErrorCode::RoutingIdConflict:
    case InnerNetworkSessionManagerErrorCode::NodeNotFound:
    case InnerNetworkSessionManagerErrorCode::RoutingIdNotFound:
        return kInnerChannelInvalid;
    case InnerNetworkSessionManagerErrorCode::InvalidArgument:
    case InnerNetworkSessionManagerErrorCode::InvalidNodeId:
        return kInnerRequestInvalid;
    }

    return kInnerRequestInvalid;
}

std::vector<xs::core::LogContextField> BuildPacketContext(
    std::span<const std::byte> routing_id,
    std::size_t payload_bytes)
{
    std::vector<xs::core::LogContextField> context;
    context.reserve(2);
    context.push_back(xs::core::LogContextField{"routingIdBytes", std::to_string(routing_id.size())});
    context.push_back(xs::core::LogContextField{"payloadBytes", std::to_string(payload_bytes)});
    return context;
}

std::vector<xs::core::LogContextField> BuildRegisterContext(
    std::span<const std::byte> routing_id,
    std::uint32_t seq,
    const xs::net::RegisterRequest* request)
{
    std::vector<xs::core::LogContextField> context;
    context.reserve(request != nullptr ? 5u : 2u);
    context.push_back(xs::core::LogContextField{"routingIdBytes", std::to_string(routing_id.size())});
    context.push_back(xs::core::LogContextField{"seq", std::to_string(seq)});

    if (request != nullptr)
    {
        context.push_back(xs::core::LogContextField{"nodeId", request->node_id});
        context.push_back(xs::core::LogContextField{"processType", BuildInnerProcessTypeText(request->process_type)});
        context.push_back(
            xs::core::LogContextField{"innerNetworkEndpoint", BuildInnerNetworkEndpointText(request->inner_network_endpoint)});
    }

    return context;
}

} // namespace

GmNode::GmNode(NodeCommandLineArgs args)
    : ServerNode(std::move(args))
{
}

GmNode::~GmNode() = default;

std::vector<InnerNetworkSession> GmNode::registry_snapshot() const
{
    return inner_network_remote_sessions().Snapshot();
}

xs::core::ProcessType GmNode::role_process_type() const noexcept
{
    return xs::core::ProcessType::Gm;
}

NodeErrorCode GmNode::OnInit()
{
    const auto* config = dynamic_cast<const xs::core::GmNodeConfig*>(&node_config());
    if (config == nullptr)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM node requires a GM-specific node configuration.");
    }

    const xs::core::EndpointConfig& endpoint = config->inner_network_listen_endpoint;
    if (endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM innerNetwork.listenEndpoint.host must not be empty.");
    }

    if (endpoint.port == 0U)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM innerNetwork.listenEndpoint.port must be greater than zero.");
    }

    const xs::core::EndpointConfig& control_endpoint = config->control_network_listen_endpoint;
    if (control_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM controlNetwork.listenEndpoint.host must not be empty.");
    }

    if (control_endpoint.port == 0U)
    {
        return SetError(
            NodeErrorCode::ConfigLoadFailed,
            "GM controlNetwork.listenEndpoint.port must be greater than zero.");
    }

    InnerNetworkOptions options;
    options.local_endpoint = BuildTcpEndpoint(endpoint);

    timeout_scan_timer_id_ = 0;
    inner_network_remote_sessions().Clear();
    InitializeClusterNodesOnlineState();
    InitializeGameToGateFullConnectionAggregationState();
    InitializeGameServiceReadyAggregationState();
    server_stub_distribute_table_.reset();
    cluster_ready_state_ = ClusterReadyState{};

    const NodeErrorCode init_result = InitInnerNetwork(std::move(options));
    if (init_result != NodeErrorCode::None)
    {
        return init_result;
    }

    inner_network()->SetListenerMessageHandler(
        [this](std::span<const std::byte> routing_id, std::span<const std::byte> payload) {
        HandleInnerMessage(routing_id, payload);
    });

    GmControlHttpServiceOptions control_options;
    control_options.listen_endpoint = control_endpoint;
    control_options.node_id = std::string(node_id());
    control_options.status_provider = [this]() {
        GmControlHttpStatusSnapshot snapshot;
        snapshot.inner_network_endpoint = inner_network() != nullptr ? std::string(inner_network()->bound_endpoint()) : "";
        snapshot.registered_process_count = static_cast<std::uint64_t>(inner_network_remote_sessions().size());
        snapshot.running = true;
        return snapshot;
    };
    control_options.stop_handler = [this]() {
        RequestStop();
    };

    control_http_service_ = std::make_unique<GmControlHttpService>(event_loop(), logger(), std::move(control_options));
    const NodeErrorCode control_init_result = control_http_service_->Init();
    if (control_init_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(control_http_service_->last_error_message());
        control_http_service_.reset();
        (void)UninitInnerNetwork();
        inner_network_remote_sessions().Clear();
        return SetError(control_init_result, error_message);
    }

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GmNode::OnRun()
{
    if (inner_network() == nullptr || control_http_service_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM node must be initialized before Run().");
    }

    const NodeErrorCode run_result = RunInnerNetwork();
    if (run_result != NodeErrorCode::None)
    {
        return run_result;
    }

    const xs::core::TimerCreateResult timeout_scan_result =
        event_loop().timers().CreateRepeating(kDefaultTimeoutScanInterval, [this]() {
            HandleTimeoutScan();
        });
    if (!xs::core::IsTimerID(timeout_scan_result))
    {
        (void)UninitInnerNetwork();
        return SetError(
            NodeErrorCode::NodeRunFailed,
            "Failed to create GM timeout scan timer: " +
                std::string(xs::core::TimerErrorMessage(xs::core::TimerErrorFromCreateResult(timeout_scan_result))));
    }
    timeout_scan_timer_id_ = timeout_scan_result;

    const NodeErrorCode control_run_result = control_http_service_->Run();
    if (control_run_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(control_http_service_->last_error_message());
        (void)event_loop().timers().Cancel(timeout_scan_timer_id_);
        timeout_scan_timer_id_ = 0;
        (void)UninitInnerNetwork();
        return SetError(control_run_result, error_message);
    }

    const std::array<xs::core::LogContextField, 3> runtime_context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"innerNetworkEndpoint", std::string(inner_network()->bound_endpoint())},
        xs::core::LogContextField{"controlNetworkEndpoint", std::string(control_http_service_->bound_endpoint())},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "GM node entered runtime state.", runtime_context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GmNode::OnUninit()
{
    if (timeout_scan_timer_id_ > 0)
    {
        (void)event_loop().timers().Cancel(timeout_scan_timer_id_);
        timeout_scan_timer_id_ = 0;
    }

    if (control_http_service_ != nullptr)
    {
        const NodeErrorCode result = control_http_service_->Uninit();
        const std::string error_message = std::string(control_http_service_->last_error_message());
        control_http_service_.reset();
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

    cluster_nodes_online_state_ = ClusterNodesOnlineState{};
    game_to_gate_full_connection_aggregation_state_ = GameToGateFullConnectionAggregationState{};
    game_service_ready_aggregation_state_ = GameServiceReadyAggregationState{};
    server_stub_distribute_table_.reset();
    cluster_ready_state_ = ClusterReadyState{};
    ClearError();
    return NodeErrorCode::None;
}

void GmNode::InitializeClusterNodesOnlineState()
{
    cluster_nodes_online_state_ = ClusterNodesOnlineState{};
    cluster_nodes_online_state_.expected_gate_node_ids.reserve(cluster_config().gates.size());
    for (const auto& [node_id, config] : cluster_config().gates)
    {
        (void)config;
        cluster_nodes_online_state_.expected_gate_node_ids.push_back(node_id);
    }

    cluster_nodes_online_state_.expected_game_node_ids.reserve(cluster_config().games.size());
    for (const auto& [node_id, config] : cluster_config().games)
    {
        (void)config;
        cluster_nodes_online_state_.expected_game_node_ids.push_back(node_id);
    }
}

void GmNode::InitializeGameToGateFullConnectionAggregationState()
{
    game_to_gate_full_connection_aggregation_state_ = GameToGateFullConnectionAggregationState{};
    game_to_gate_full_connection_aggregation_state_.entries.reserve(cluster_config().games.size());
    for (const auto& [node_id, config] : cluster_config().games)
    {
        (void)config;
        game_to_gate_full_connection_aggregation_state_.entries.push_back(
            GameMeshReadyEntry{
                .node_id = node_id,
            });
    }
}

void GmNode::InitializeGameServiceReadyAggregationState()
{
    game_service_ready_aggregation_state_ = GameServiceReadyAggregationState{};
    game_service_ready_aggregation_state_.entries.reserve(cluster_config().games.size());
    for (const auto& [node_id, config] : cluster_config().games)
    {
        (void)config;
        game_service_ready_aggregation_state_.entries.push_back(
            GameServiceReadyEntry{
                .node_id = node_id,
            });
    }
}

GmNode::GameMeshReadyEntry* GmNode::mesh_ready_entry(std::string_view node_id) noexcept
{
    auto iterator = std::find_if(
        game_to_gate_full_connection_aggregation_state_.entries.begin(),
        game_to_gate_full_connection_aggregation_state_.entries.end(),
        [node_id](const GameMeshReadyEntry& entry) {
            return entry.node_id == node_id;
        });
    return iterator != game_to_gate_full_connection_aggregation_state_.entries.end() ? &(*iterator) : nullptr;
}

const GmNode::GameMeshReadyEntry* GmNode::mesh_ready_entry(std::string_view node_id) const noexcept
{
    auto iterator = std::find_if(
        game_to_gate_full_connection_aggregation_state_.entries.begin(),
        game_to_gate_full_connection_aggregation_state_.entries.end(),
        [node_id](const GameMeshReadyEntry& entry) {
            return entry.node_id == node_id;
        });
    return iterator != game_to_gate_full_connection_aggregation_state_.entries.end() ? &(*iterator) : nullptr;
}

GmNode::GameServiceReadyEntry* GmNode::service_ready_entry(std::string_view node_id) noexcept
{
    auto iterator = std::find_if(
        game_service_ready_aggregation_state_.entries.begin(),
        game_service_ready_aggregation_state_.entries.end(),
        [node_id](const GameServiceReadyEntry& entry) {
            return entry.node_id == node_id;
        });
    return iterator != game_service_ready_aggregation_state_.entries.end() ? &(*iterator) : nullptr;
}

const GmNode::GameServiceReadyEntry* GmNode::service_ready_entry(std::string_view node_id) const noexcept
{
    auto iterator = std::find_if(
        game_service_ready_aggregation_state_.entries.begin(),
        game_service_ready_aggregation_state_.entries.end(),
        [node_id](const GameServiceReadyEntry& entry) {
            return entry.node_id == node_id;
        });
    return iterator != game_service_ready_aggregation_state_.entries.end() ? &(*iterator) : nullptr;
}

bool GmNode::AreAllExpectedNodesOnline() const noexcept
{
    const auto is_online = [this](std::string_view node_id) {
        const InnerNetworkSession* session = inner_network_remote_sessions().FindByNodeId(node_id);
        return session != nullptr &&
               session->registered &&
               !session->heartbeat_timed_out;
    };

    const std::size_t expected_node_count =
        cluster_nodes_online_state_.expected_gate_node_ids.size() +
        cluster_nodes_online_state_.expected_game_node_ids.size();
    if (expected_node_count == 0U)
    {
        return false;
    }

    return std::all_of(
               cluster_nodes_online_state_.expected_gate_node_ids.begin(),
               cluster_nodes_online_state_.expected_gate_node_ids.end(),
               is_online) &&
           std::all_of(
               cluster_nodes_online_state_.expected_game_node_ids.begin(),
               cluster_nodes_online_state_.expected_game_node_ids.end(),
               is_online);
}

bool GmNode::AreAllExpectedGamesMeshReady() const noexcept
{
    if (game_to_gate_full_connection_aggregation_state_.entries.empty())
    {
        return false;
    }

    return std::all_of(
        game_to_gate_full_connection_aggregation_state_.entries.begin(),
        game_to_gate_full_connection_aggregation_state_.entries.end(),
        [](const GameMeshReadyEntry& entry) {
            return entry.mesh_ready;
        });
}

bool GmNode::AreAllServerStubsReady() const noexcept
{
    if (server_stub_distribute_table_ == nullptr || server_stub_distribute_table_->assignments.empty())
    {
        return false;
    }

    return std::all_of(
        server_stub_distribute_table_->assignments.begin(),
        server_stub_distribute_table_->assignments.end(),
        [this](const xs::net::ServerStubOwnershipEntry& assignment) {
            const GameServiceReadyEntry* entry = service_ready_entry(assignment.owner_game_node_id);
            if (entry == nullptr || entry->assignment_epoch != kServerStubOwnershipAssignmentEpoch)
            {
                return false;
            }

            const auto iterator = std::find_if(
                entry->ready_entries.begin(),
                entry->ready_entries.end(),
                [&assignment](const xs::net::ServerStubReadyEntry& ready_entry) {
                    return ready_entry.entity_type == assignment.entity_type &&
                        ready_entry.entity_key == assignment.entity_key &&
                        ready_entry.ready;
                });
            return iterator != entry->ready_entries.end();
        });
}

void GmNode::InvalidateAllGameMeshReadyState()
{
    for (GameMeshReadyEntry& entry : game_to_gate_full_connection_aggregation_state_.entries)
    {
        entry.mesh_ready = false;
        entry.reported_at_unix_ms = 0U;
    }

    game_to_gate_full_connection_aggregation_state_.all_expected_games_mesh_ready = false;
}

void GmNode::InvalidateGameMeshReadyState(std::string_view game_node_id)
{
    GameMeshReadyEntry* entry = mesh_ready_entry(game_node_id);
    if (entry == nullptr)
    {
        return;
    }

    entry->mesh_ready = false;
    entry->reported_at_unix_ms = 0U;
    game_to_gate_full_connection_aggregation_state_.all_expected_games_mesh_ready = false;
}

void GmNode::RefreshClusterNodesOnlineState(std::string_view trigger_node_id)
{
    const bool all_nodes_online = AreAllExpectedNodesOnline();
    const bool state_changed = cluster_nodes_online_state_.all_nodes_online != all_nodes_online;
    if (!state_changed)
    {
        RefreshServerStubDistributeTable();
        return;
    }

    const std::uint64_t server_now_unix_ms = CurrentUnixTimeMilliseconds();
    cluster_nodes_online_state_.all_nodes_online = all_nodes_online;
    cluster_nodes_online_state_.last_server_now_unix_ms = server_now_unix_ms;

    if (!all_nodes_online)
    {
        InvalidateAllGameMeshReadyState();
        InitializeGameServiceReadyAggregationState();
        cluster_ready_state_ = ClusterReadyState{};
    }

    std::uint64_t notify_target_count = 0U;
    for (const std::string& node_id : cluster_nodes_online_state_.expected_game_node_ids)
    {
        const InnerNetworkSession* session = inner_network_remote_sessions().FindByNodeId(node_id);
        if (session == nullptr || !session->registered || session->heartbeat_timed_out)
        {
            continue;
        }

        ++notify_target_count;
        SendClusterNodesOnlineNotifyToGame(*session, all_nodes_online, server_now_unix_ms);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"allNodesOnline", all_nodes_online ? "true" : "false"},
        xs::core::LogContextField{"notifyTargetCount", ToString(notify_target_count)},
        xs::core::LogContextField{"expectedGameCount", ToString(cluster_nodes_online_state_.expected_game_node_ids.size())},
        xs::core::LogContextField{"expectedGateCount", ToString(cluster_nodes_online_state_.expected_gate_node_ids.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM refreshed cluster nodes online state.", context);
    RefreshServerStubDistributeTable();
}

GmNode::ServerStubDistributeTable GmNode::BuildServerStubDistributeTable() const
{
    struct RandomServerStubDistributeStrategy final
    {
        explicit RandomServerStubDistributeStrategy(std::mt19937_64 random_engine)
            : random_engine_(std::move(random_engine))
        {
        }

        [[nodiscard]] GmNode::ServerStubDistributeTable Build(
            const std::vector<std::string>& candidate_game_node_ids)
        {
            GmNode::ServerStubDistributeTable table{};
            table.assignments.reserve(kBootstrapStubCatalog.size());

            if (candidate_game_node_ids.empty())
            {
                return table;
            }

            std::uniform_int_distribution<std::size_t> distribution(0u, candidate_game_node_ids.size() - 1u);

            for (const BootstrapStubDefinition& stub : kBootstrapStubCatalog)
            {
                table.assignments.push_back(
                    xs::net::ServerStubOwnershipEntry{
                        .entity_type = std::string(stub.entity_type),
                        .entity_key = std::string(stub.entity_key),
                        .owner_game_node_id = candidate_game_node_ids[distribution(random_engine_)],
                        .entry_flags = 0U,
                    });
            }

            return table;
        }

      private:
        std::mt19937_64 random_engine_;
    };

    RandomServerStubDistributeStrategy strategy{std::mt19937_64{std::random_device{}()}};
    return strategy.Build(cluster_nodes_online_state_.expected_game_node_ids);
}

void GmNode::RefreshServerStubDistributeTable()
{
    const bool all_expected_games_mesh_ready = cluster_nodes_online_state_.all_nodes_online && AreAllExpectedGamesMeshReady();
    if (game_to_gate_full_connection_aggregation_state_.all_expected_games_mesh_ready == all_expected_games_mesh_ready)
    {
        return;
    }

    game_to_gate_full_connection_aggregation_state_.all_expected_games_mesh_ready = all_expected_games_mesh_ready;
    if (!all_expected_games_mesh_ready)
    {
        InitializeGameServiceReadyAggregationState();
        cluster_ready_state_ = ClusterReadyState{};
        return;
    }

    if (server_stub_distribute_table_ == nullptr)
    {
        server_stub_distribute_table_ = std::make_unique<ServerStubDistributeTable>(BuildServerStubDistributeTable());
    }

    InitializeGameServiceReadyAggregationState();
    cluster_ready_state_ = ClusterReadyState{};

    const ServerStubDistributeTable& server_stub_distribute_table = *server_stub_distribute_table_;
    const std::uint64_t server_now_unix_ms = CurrentUnixTimeMilliseconds();

    const xs::net::ServerStubOwnershipSync sync
    {
        .assignment_epoch = kServerStubOwnershipAssignmentEpoch,
        .status_flags = 0U,
        .assignments = server_stub_distribute_table.assignments,
        .server_now_unix_ms = server_now_unix_ms,
    };

    std::uint64_t notify_target_count = 0U;
    for (const std::string& node_id : cluster_nodes_online_state_.expected_game_node_ids)
    {
        const InnerNetworkSession* session = inner_network_remote_sessions().FindByNodeId(node_id);
        if (session == nullptr || !session->registered || session->heartbeat_timed_out)
        {
            continue;
        }

        ++notify_target_count;
        SendOwnershipSyncToGame(*session, sync);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"assignmentEpoch", ToString(kServerStubOwnershipAssignmentEpoch)},
        xs::core::LogContextField{"assignmentCount", ToString(server_stub_distribute_table.assignments.size())},
        xs::core::LogContextField{"notifyTargetCount", ToString(notify_target_count)},
        xs::core::LogContextField{"expectedGameCount", ToString(cluster_nodes_online_state_.expected_game_node_ids.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM refreshed server stub distribute table.", context);
}

void GmNode::RefreshClusterReadyState()
{
    const bool next_cluster_ready = AreAllServerStubsReady();
    if (cluster_ready_state_.cluster_ready == next_cluster_ready)
    {
        return;
    }

    const std::uint64_t server_now_unix_ms = CurrentUnixTimeMilliseconds();
    cluster_ready_state_.cluster_ready = next_cluster_ready;
    cluster_ready_state_.last_server_now_unix_ms = server_now_unix_ms;
    if (!next_cluster_ready)
    {
        return;
    }

    ++cluster_ready_state_.ready_epoch;
    const xs::net::ClusterReadyNotify notify{
        .ready_epoch = cluster_ready_state_.ready_epoch,
        .cluster_ready = true,
        .status_flags = 0U,
        .server_now_unix_ms = server_now_unix_ms,
    };

    std::uint64_t notify_target_count = 0U;
    for (const std::string& node_id : cluster_nodes_online_state_.expected_gate_node_ids)
    {
        const InnerNetworkSession* session = inner_network_remote_sessions().FindByNodeId(node_id);
        if (session == nullptr || !session->registered || session->heartbeat_timed_out)
        {
            continue;
        }

        ++notify_target_count;
        SendClusterReadyNotifyToGate(*session, notify);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
        xs::core::LogContextField{"clusterReady", notify.cluster_ready ? "true" : "false"},
        xs::core::LogContextField{"notifyTargetCount", ToString(notify_target_count)},
        xs::core::LogContextField{"assignmentCount", server_stub_distribute_table_ != nullptr ? ToString(server_stub_distribute_table_->assignments.size()) : "0"},
        xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM refreshed cluster ready state.", context);
}

void GmNode::SendClusterNodesOnlineNotifyToGame(
    const InnerNetworkSession& session,
    bool all_nodes_online,
    std::uint64_t server_now_unix_ms)
{
    if (inner_network() == nullptr)
    {
        return;
    }

    const xs::net::ClusterNodesOnlineNotify notify{
        .all_nodes_online = all_nodes_online,
        .status_flags = 0U,
        .server_now_unix_ms = server_now_unix_ms,
    };

    std::array<std::byte, xs::net::kClusterNodesOnlineNotifySize> body{};
    const xs::net::InnerClusterCodecErrorCode body_result =
        xs::net::EncodeClusterNodesOnlineNotify(notify, body);
    if (body_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"targetGameNodeId", session.node_id},
            xs::core::LogContextField{"allNodesOnline", all_nodes_online ? "true" : "false"},
            xs::core::LogContextField{"codecError", std::string(xs::net::InnerClusterCodecErrorMessage(body_result))},
            xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to encode cluster nodes online notify.", context);
        return;
    }

    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerClusterNodesOnlineNotifyMsgId,
        xs::net::kPacketSeqNone,
        0U,
        static_cast<std::uint32_t>(body.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kClusterNodesOnlineNotifySize> packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, body, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"targetGameNodeId", session.node_id},
            xs::core::LogContextField{"allNodesOnline", all_nodes_online ? "true" : "false"},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
            xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to wrap cluster nodes online notify packet.", context);
        return;
    }

    const NodeErrorCode send_result = inner_network()->Send(session.routing_id, packet);
    if (send_result != NodeErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"targetGameNodeId", session.node_id},
            xs::core::LogContextField{"allNodesOnline", all_nodes_online ? "true" : "false"},
            xs::core::LogContextField{"routingIdBytes", ToString(session.routing_id.size())},
            xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
            xs::core::LogContextField{"innerNetworkError", std::string(inner_network()->last_error_message())},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to send cluster nodes online notify.", context);
        return;
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"targetGameNodeId", session.node_id},
        xs::core::LogContextField{"allNodesOnline", all_nodes_online ? "true" : "false"},
        xs::core::LogContextField{"routingIdBytes", ToString(session.routing_id.size())},
        xs::core::LogContextField{"payloadBytes", ToString(packet.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM sent cluster nodes online notify.", context);
}

void GmNode::SendOwnershipSyncToGame(
    const InnerNetworkSession& session,
    const xs::net::ServerStubOwnershipSync& sync)
{
    if (inner_network() == nullptr)
    {
        return;
    }

    std::size_t wire_size = 0U;
    const xs::net::InnerClusterCodecErrorCode size_result =
        xs::net::GetServerStubOwnershipSyncWireSize(sync, &wire_size);
    if (size_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"targetGameNodeId", session.node_id},
            xs::core::LogContextField{"assignmentEpoch", ToString(sync.assignment_epoch)},
            xs::core::LogContextField{"codecError", std::string(xs::net::InnerClusterCodecErrorMessage(size_result))},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to size ownership sync.", context);
        return;
    }

    std::vector<std::byte> body(wire_size);
    const xs::net::InnerClusterCodecErrorCode body_result =
        xs::net::EncodeServerStubOwnershipSync(sync, body);
    if (body_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"targetGameNodeId", session.node_id},
            xs::core::LogContextField{"assignmentEpoch", ToString(sync.assignment_epoch)},
            xs::core::LogContextField{"codecError", std::string(xs::net::InnerClusterCodecErrorMessage(body_result))},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to encode ownership sync.", context);
        return;
    }

    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerServerStubOwnershipSyncMsgId,
        xs::net::kPacketSeqNone,
        0U,
        static_cast<std::uint32_t>(body.size()));
    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, body, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"targetGameNodeId", session.node_id},
            xs::core::LogContextField{"assignmentEpoch", ToString(sync.assignment_epoch)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to wrap ownership sync into a packet.", context);
        return;
    }

    const NodeErrorCode send_result = inner_network()->Send(session.routing_id, packet);
    if (send_result != NodeErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"targetGameNodeId", session.node_id},
            xs::core::LogContextField{"assignmentEpoch", ToString(sync.assignment_epoch)},
            xs::core::LogContextField{"routingIdBytes", ToString(session.routing_id.size())},
            xs::core::LogContextField{"innerNetworkError", std::string(inner_network()->last_error_message())},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to send ownership sync.", context);
        return;
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"targetGameNodeId", session.node_id},
        xs::core::LogContextField{"assignmentEpoch", ToString(sync.assignment_epoch)},
        xs::core::LogContextField{"assignmentCount", ToString(sync.assignments.size())},
        xs::core::LogContextField{"routingIdBytes", ToString(session.routing_id.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(sync.server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM sent ownership sync.", context);
}

void GmNode::SendClusterReadyNotifyToGate(
    const InnerNetworkSession& session,
    const xs::net::ClusterReadyNotify& notify)
{
    if (inner_network() == nullptr)
    {
        return;
    }

    std::array<std::byte, xs::net::kClusterReadyNotifySize> body{};
    const xs::net::InnerClusterCodecErrorCode body_result =
        xs::net::EncodeClusterReadyNotify(notify, body);
    if (body_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"targetGateNodeId", session.node_id},
            xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
            xs::core::LogContextField{"codecError", std::string(xs::net::InnerClusterCodecErrorMessage(body_result))},
            xs::core::LogContextField{"serverNowUnixMs", ToString(notify.server_now_unix_ms)},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to encode cluster ready notify.", context);
        return;
    }

    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerClusterReadyNotifyMsgId,
        xs::net::kPacketSeqNone,
        0U,
        static_cast<std::uint32_t>(body.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kClusterReadyNotifySize> packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, body, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"targetGateNodeId", session.node_id},
            xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
            xs::core::LogContextField{"serverNowUnixMs", ToString(notify.server_now_unix_ms)},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to wrap cluster ready notify packet.", context);
        return;
    }

    const NodeErrorCode send_result = inner_network()->Send(session.routing_id, packet);
    if (send_result != NodeErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"targetGateNodeId", session.node_id},
            xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
            xs::core::LogContextField{"routingIdBytes", ToString(session.routing_id.size())},
            xs::core::LogContextField{"serverNowUnixMs", ToString(notify.server_now_unix_ms)},
            xs::core::LogContextField{"innerNetworkError", std::string(inner_network()->last_error_message())},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to send cluster ready notify.", context);
        return;
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"targetGateNodeId", session.node_id},
        xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
        xs::core::LogContextField{"clusterReady", notify.cluster_ready ? "true" : "false"},
        xs::core::LogContextField{"routingIdBytes", ToString(session.routing_id.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(notify.server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM sent cluster ready notify.", context);
}

void GmNode::HandleInnerMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    xs::net::PacketHeader raw_header{};
    if (!TryReadRawPacketHeader(payload, &raw_header))
    {
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored a payload without a complete packet header.", context);
        return;
    }

    if (raw_header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        HandleRegisterMessage(routing_id, payload);
        return;
    }

    if (raw_header.msg_id == xs::net::kInnerHeartbeatMsgId)
    {
        HandleHeartbeatMessage(routing_id, payload);
        return;
    }

    if (raw_header.msg_id == xs::net::kInnerGameGateMeshReadyReportMsgId)
    {
        HandleGameGateMeshReadyReport(routing_id, payload);
        return;
    }

    if (raw_header.msg_id == xs::net::kInnerGameServiceReadyReportMsgId)
    {
        HandleGameServiceReadyReport(routing_id, payload);
        return;
    }

    std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
    context.push_back(xs::core::LogContextField{"msgId", std::to_string(raw_header.msg_id)});
    context.push_back(xs::core::LogContextField{"seq", std::to_string(raw_header.seq)});
    context.push_back(xs::core::LogContextField{"flags", BuildPacketFlagsText(raw_header.flags)});
    logger().Log(xs::core::LogLevel::Info, "inner", "GM ignored an unsupported inner packet.", context);
}

void GmNode::HandleRegisterMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM dropped malformed register packet.", context);
        return;
    }

    if (HasPacketFlag(packet.header.flags, xs::net::PacketFlag::Response) ||
        HasPacketFlag(packet.header.flags, xs::net::PacketFlag::Error) ||
        packet.header.seq == xs::net::kPacketSeqNone)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"seq", std::to_string(packet.header.seq)});
        context.push_back(xs::core::LogContextField{"flags", BuildPacketFlagsText(packet.header.flags)});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored invalid register packet envelope.", context);
        return;
    }

    auto send_response = [this, routing_id, &packet](std::uint16_t flags, std::span<const std::byte> response_payload) -> bool {
        const xs::net::PacketHeader response_header =
            xs::net::MakePacketHeader(
                xs::net::kInnerRegisterMsgId,
                packet.header.seq,
                flags,
                static_cast<std::uint32_t>(response_payload.size()));

        std::vector<std::byte> response_buffer(
            xs::net::kPacketHeaderSize + response_payload.size());
        const xs::net::PacketCodecErrorCode encode_result =
            xs::net::EncodePacket(response_header, response_payload, response_buffer);
        if (encode_result != xs::net::PacketCodecErrorCode::None)
        {
            std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, response_buffer.size());
            context.push_back(xs::core::LogContextField{"seq", std::to_string(packet.header.seq)});
            context.push_back(
                xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(encode_result))});
            logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to encode register response packet.", context);
            return false;
        }

        if (inner_network() == nullptr)
        {
            return false;
        }

        const NodeErrorCode send_result = inner_network()->Send(routing_id, response_buffer);
        if (send_result != NodeErrorCode::None)
        {
            std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, response_buffer.size());
            context.push_back(xs::core::LogContextField{"seq", std::to_string(packet.header.seq)});
            logger().Log(
                xs::core::LogLevel::Error,
                "inner",
                "GM failed to send register response packet.",
                context);
            return false;
        }

        return true;
    };

    auto send_error_response = [this, &send_response, routing_id, &packet](
                                   std::int32_t error_code,
                                   const xs::net::RegisterRequest* request) {
        const xs::net::RegisterErrorResponse response{
            .error_code = error_code,
            .retry_after_ms = 0u,
        };

        std::array<std::byte, xs::net::kRegisterErrorResponseSize> payload_buffer{};
        const xs::net::RegisterCodecErrorCode encode_result =
            xs::net::EncodeRegisterErrorResponse(response, payload_buffer);
        if (encode_result != xs::net::RegisterCodecErrorCode::None)
        {
            std::vector<xs::core::LogContextField> context = BuildRegisterContext(routing_id, packet.header.seq, request);
            context.push_back(
                xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(encode_result))});
            logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to encode register error response.", context);
            return;
        }

        if (!send_response(
                static_cast<std::uint16_t>(xs::net::PacketFlag::Response) |
                    static_cast<std::uint16_t>(xs::net::PacketFlag::Error),
                payload_buffer))
        {
            return;
        }

        std::vector<xs::core::LogContextField> context = BuildRegisterContext(routing_id, packet.header.seq, request);
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "GM rejected register request.",
            context,
            error_code,
            InnerErrorName(error_code));
    };

    xs::net::RegisterRequest request{};
    const xs::net::RegisterCodecErrorCode decode_result = xs::net::DecodeRegisterRequest(packet.payload, &request);
    if (decode_result != xs::net::RegisterCodecErrorCode::None)
    {
        const std::optional<std::int32_t> error_code = MapRegisterCodecErrorToInnerError(decode_result);

        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"seq", std::to_string(packet.header.seq)});
        context.push_back(xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(decode_result))});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM failed to decode register request payload.", context);

        if (error_code.has_value())
        {
            send_error_response(*error_code, nullptr);
        }

        return;
    }

    const std::uint64_t server_now_unix_ms = CurrentUnixTimeMilliseconds();
    const std::optional<xs::core::ProcessType> process_type = ToCoreProcessType(request.process_type);
    if (!process_type.has_value())
    {
        send_error_response(kInnerProcessTypeInvalid, &request);
        return;
    }

    InnerNetworkSessionRegistration registration{
        .process_type = *process_type,
        .node_id = request.node_id,
        .pid = request.pid,
        .started_at_unix_ms = request.started_at_unix_ms,
        .inner_network_endpoint = request.inner_network_endpoint,
        .build_version = request.build_version,
        .capability_tags = request.capability_tags,
        .load = request.load,
        .routing_id = RoutingID(routing_id.begin(), routing_id.end()),
        .last_heartbeat_at_unix_ms = server_now_unix_ms,
        .inner_network_ready = false,
    };

    auto apply_registration_state = [&](InnerNetworkSession& session) {
        session.process_type = registration.process_type;
        session.node_id = registration.node_id;
        session.pid = registration.pid;
        session.started_at_unix_ms = registration.started_at_unix_ms;
        session.inner_network_endpoint = registration.inner_network_endpoint;
        session.build_version = registration.build_version;
        session.capability_tags = registration.capability_tags;
        session.load = registration.load;
        session.routing_id = registration.routing_id;
        session.last_heartbeat_at_unix_ms = registration.last_heartbeat_at_unix_ms;
        session.inner_network_ready = registration.inner_network_ready;
        session.heartbeat_timed_out = false;
        session.registered = true;
        session.connection_state = ipc::ZmqConnectionState::Connected;
        session.heartbeat_interval_ms = kDefaultHeartbeatIntervalMs;
        session.heartbeat_timeout_ms = kDefaultHeartbeatTimeoutMs;
        session.last_server_now_unix_ms = server_now_unix_ms;
        session.last_protocol_error.clear();
    };

    InnerNetworkSession* session = inner_network_remote_sessions().FindMutableByNodeId(request.node_id);
    if (session != nullptr)
    {
        const bool same_routing_id = session->routing_id == registration.routing_id;
        if (!same_routing_id && !session->heartbeat_timed_out)
        {
            send_error_response(kInnerNodeIdConflict, &request);
            return;
        }

        if (!same_routing_id)
        {
            const InnerNetworkSessionManagerErrorCode unregister_result =
                inner_network_remote_sessions().UnregisterByNodeId(request.node_id);
            if (unregister_result != InnerNetworkSessionManagerErrorCode::None)
            {
                std::vector<xs::core::LogContextField> context =
                    BuildRegisterContext(routing_id, packet.header.seq, &request);
                context.push_back(
                    xs::core::LogContextField{
                        "sessionManagerError",
                        std::string(InnerNetworkSessionManagerErrorMessage(unregister_result))});
                logger().Log(
                    xs::core::LogLevel::Warn,
                    "inner",
                    "GM failed to replace an existing timed-out register session.",
                    context);
                return;
            }

            session = nullptr;
        }
    }

    if (session == nullptr)
    {
        const InnerNetworkSessionManagerErrorCode register_result =
            inner_network_remote_sessions().Register(registration);
        if (register_result != InnerNetworkSessionManagerErrorCode::None)
        {
            const std::optional<std::int32_t> error_code =
                MapInnerNetworkSessionManagerErrorToInnerError(register_result);
            if (error_code.has_value())
            {
                send_error_response(*error_code, &request);
            }
            else
            {
                std::vector<xs::core::LogContextField> context =
                    BuildRegisterContext(routing_id, packet.header.seq, &request);
                context.push_back(
                    xs::core::LogContextField{
                        "sessionManagerError",
                        std::string(InnerNetworkSessionManagerErrorMessage(register_result))});
                logger().Log(
                    xs::core::LogLevel::Warn,
                    "inner",
                    "GM rejected register request without a mapped session manager error code.",
                    context);
            }

            return;
        }

        session = inner_network_remote_sessions().FindMutableByNodeId(request.node_id);
    }

    if (session != nullptr)
    {
        apply_registration_state(*session);
    }

    if (registration.process_type == xs::core::ProcessType::Game)
    {
        InvalidateGameMeshReadyState(registration.node_id);
    }

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
        std::vector<xs::core::LogContextField> context = BuildRegisterContext(routing_id, packet.header.seq, &request);
        context.push_back(
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(encode_result))});
        logger().Log(xs::core::LogLevel::Error, "inner", "GM failed to encode register success response.", context);
        return;
    }

    if (!send_response(static_cast<std::uint16_t>(xs::net::PacketFlag::Response), payload_buffer))
    {
        return;
    }

    std::vector<xs::core::LogContextField> context = BuildRegisterContext(routing_id, packet.header.seq, &request);
    logger().Log(xs::core::LogLevel::Info, "inner", "GM accepted register request.", context);

    RefreshClusterNodesOnlineState(request.node_id);
}

void GmNode::HandleHeartbeatMessage(
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
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM inner service ignored a payload without a complete packet header.", context);
        return;
    }

    if (!IsHeartbeatRequestPacket(raw_header))
    {
        return;
    }

    auto log_heartbeat_rejected = [this, routing_id, &raw_header](
                                      std::int32_t error_code,
                                      std::string_view error_name,
                                      std::string_view log_message) {
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"seq", ToString(static_cast<std::uint64_t>(raw_header.seq))},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", log_message, context, error_code, error_name);
    };

    if (!xs::net::IsValidPacketFlags(raw_header.flags) ||
        raw_header.flags != 0U ||
        raw_header.seq == xs::net::kPacketSeqNone)
    {
        log_heartbeat_rejected(
            kInnerRequestInvalid,
            "Inner.RequestInvalid",
            "GM inner service ignored an invalid heartbeat request.");
        return;
    }

    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode decode_packet_result = xs::net::DecodePacket(payload, &packet);
    if (decode_packet_result != xs::net::PacketCodecErrorCode::None)
    {
        log_heartbeat_rejected(
            kInnerRequestInvalid,
            "Inner.RequestInvalid",
            "GM inner service ignored a malformed heartbeat packet.");
        return;
    }

    xs::net::HeartbeatRequest request{};
    const xs::net::HeartbeatCodecErrorCode decode_request_result =
        xs::net::DecodeHeartbeatRequest(packet.payload, &request);
    if (decode_request_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        log_heartbeat_rejected(
            kInnerRequestInvalid,
            "Inner.RequestInvalid",
            "GM inner service ignored a malformed heartbeat payload.");
        return;
    }

    InnerNetworkSession* session = inner_network_remote_sessions().FindMutableByRoutingId(routing_id);
    if (session == nullptr)
    {
        log_heartbeat_rejected(
            kInnerNodeNotRegistered,
            "Inner.NodeNotRegistered",
            "GM inner service ignored heartbeat from an unknown inner channel.");
        return;
    }

    const bool cluster_nodes_online_state_may_change =
        session->heartbeat_timed_out ||
        !session->registered;
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
    if (xs::net::EncodeHeartbeatSuccessResponse(response, response_body) != xs::net::HeartbeatCodecErrorCode::None)
    {
        return;
    }

    const xs::net::PacketHeader response_header = xs::net::MakePacketHeader(
        xs::net::kInnerHeartbeatMsgId,
        packet.header.seq,
        static_cast<std::uint16_t>(xs::net::PacketFlag::Response),
        static_cast<std::uint32_t>(response_body.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kHeartbeatSuccessResponseSize> response_packet{};
    if (xs::net::EncodePacket(response_header, response_body, response_packet) != xs::net::PacketCodecErrorCode::None)
    {
        return;
    }

    if (inner_network() == nullptr)
    {
        return;
    }

    const NodeErrorCode send_result = inner_network()->Send(routing_id, response_packet);
    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(session->node_id)},
        xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
        xs::core::LogContextField{"seq", ToString(static_cast<std::uint64_t>(packet.header.seq))},
        xs::core::LogContextField{"loadScore", ToString(static_cast<std::uint64_t>(request.load.load_score))},
    };
    if (send_result != NodeErrorCode::None)
    {
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM inner service failed to send heartbeat success response.", context);
        return;
    }

    logger().Log(xs::core::LogLevel::Info, "inner", "GM inner service refreshed heartbeat state.", context);
    if (cluster_nodes_online_state_may_change)
    {
        RefreshClusterNodesOnlineState();
    }
}

void GmNode::HandleGameGateMeshReadyReport(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM dropped malformed mesh ready report packet.", context);
        return;
    }

    if (packet.header.flags != 0U || packet.header.seq != xs::net::kPacketSeqNone)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"seq", std::to_string(packet.header.seq)});
        context.push_back(xs::core::LogContextField{"flags", BuildPacketFlagsText(packet.header.flags)});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored mesh ready report with an invalid envelope.", context);
        return;
    }

    xs::net::GameGateMeshReadyReport report{};
    const xs::net::InnerClusterCodecErrorCode decode_result =
        xs::net::DecodeGameGateMeshReadyReport(packet.payload, &report);
    if (decode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(
            xs::core::LogContextField{"codecError", std::string(xs::net::InnerClusterCodecErrorMessage(decode_result))});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM failed to decode mesh ready report payload.", context);
        return;
    }

    InnerNetworkSession* session = inner_network_remote_sessions().FindMutableByRoutingId(routing_id);
    if (session == nullptr ||
        session->process_type != xs::core::ProcessType::Game ||
        !session->registered ||
        session->heartbeat_timed_out)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"meshReady", report.mesh_ready ? "true" : "false"});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored mesh ready report from an unregistered Game session.", context);
        return;
    }

    if (!cluster_nodes_online_state_.all_nodes_online)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"nodeId", session->node_id});
        context.push_back(xs::core::LogContextField{"meshReady", report.mesh_ready ? "true" : "false"});
        logger().Log(xs::core::LogLevel::Info, "inner", "GM ignored mesh ready report while allNodesOnline is false.", context);
        return;
    }

    GameMeshReadyEntry* entry = mesh_ready_entry(session->node_id);
    if (entry == nullptr)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"nodeId", session->node_id});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored mesh ready report from an unexpected Game node.", context);
        return;
    }

    entry->mesh_ready = report.mesh_ready;
    entry->reported_at_unix_ms = report.reported_at_unix_ms;

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", session->node_id},
        xs::core::LogContextField{"meshReady", report.mesh_ready ? "true" : "false"},
        xs::core::LogContextField{"reportedAtUnixMs", ToString(report.reported_at_unix_ms)},
        xs::core::LogContextField{"allNodesOnline", cluster_nodes_online_state_.all_nodes_online ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM accepted mesh ready report.", context);

    RefreshServerStubDistributeTable();
}

void GmNode::HandleGameServiceReadyReport(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM dropped malformed service ready report packet.", context);
        return;
    }

    if (packet.header.flags != 0U || packet.header.seq != xs::net::kPacketSeqNone)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"seq", std::to_string(packet.header.seq)});
        context.push_back(xs::core::LogContextField{"flags", BuildPacketFlagsText(packet.header.flags)});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored service ready report with an invalid envelope.", context);
        return;
    }

    xs::net::GameServiceReadyReport report{};
    const xs::net::InnerClusterCodecErrorCode decode_result =
        xs::net::DecodeGameServiceReadyReport(packet.payload, &report);
    if (decode_result != xs::net::InnerClusterCodecErrorCode::None)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(
            xs::core::LogContextField{"codecError", std::string(xs::net::InnerClusterCodecErrorMessage(decode_result))});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM failed to decode service ready report payload.", context);
        return;
    }

    InnerNetworkSession* session = inner_network_remote_sessions().FindMutableByRoutingId(routing_id);
    if (session == nullptr ||
        session->process_type != xs::core::ProcessType::Game ||
        !session->registered ||
        session->heartbeat_timed_out)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored service ready report from an unregistered Game session.", context);
        return;
    }

    if (!cluster_nodes_online_state_.all_nodes_online || server_stub_distribute_table_ == nullptr)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"nodeId", session->node_id});
        context.push_back(xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)});
        logger().Log(xs::core::LogLevel::Info, "inner", "GM ignored service ready report before ownership sync became active.", context);
        return;
    }

    if (report.assignment_epoch != kServerStubOwnershipAssignmentEpoch)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", session->node_id},
            xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
            xs::core::LogContextField{"expectedAssignmentEpoch", ToString(kServerStubOwnershipAssignmentEpoch)},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "GM ignored stale service ready report.", context);
        return;
    }

    GameServiceReadyEntry* entry = service_ready_entry(session->node_id);
    if (entry == nullptr)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"nodeId", session->node_id});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored service ready report from an unexpected Game node.", context);
        return;
    }

    const std::size_t ready_entry_count = report.entries.size();
    entry->assignment_epoch = report.assignment_epoch;
    entry->local_ready = report.local_ready;
    entry->reported_at_unix_ms = report.reported_at_unix_ms;
    entry->ready_entries = std::move(report.entries);

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", session->node_id},
        xs::core::LogContextField{"assignmentEpoch", ToString(entry->assignment_epoch)},
        xs::core::LogContextField{"localReady", entry->local_ready ? "true" : "false"},
        xs::core::LogContextField{"readyEntryCount", ToString(ready_entry_count)},
        xs::core::LogContextField{"reportedAtUnixMs", ToString(entry->reported_at_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM accepted service ready report.", context);

    RefreshClusterReadyState();
}

void GmNode::HandleTimeoutScan()
{
    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();

    const std::vector<InnerNetworkSession> snapshot = inner_network_remote_sessions().Snapshot();
    bool cluster_nodes_online_needs_refresh = false;
    for (const InnerNetworkSession& entry : snapshot)
    {
        if (entry.last_heartbeat_at_unix_ms == 0U || entry.last_heartbeat_at_unix_ms > now_unix_ms)
        {
            continue;
        }

        const std::uint64_t elapsed_ms = now_unix_ms - entry.last_heartbeat_at_unix_ms;
        if (elapsed_ms < static_cast<std::uint64_t>(kDefaultHeartbeatTimeoutMs))
        {
            continue;
        }

        InnerNetworkSession* session = inner_network_remote_sessions().FindMutableByNodeId(entry.node_id);
        if (session == nullptr || session->heartbeat_timed_out)
        {
            continue;
        }

        session->heartbeat_timed_out = true;
        session->inner_network_ready = false;
        session->registered = false;
        session->connection_state = ipc::ZmqConnectionState::Stopped;
        session->last_protocol_error = "Heartbeat timed out.";
        cluster_nodes_online_needs_refresh = true;

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", entry.node_id},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(entry.routing_id.size()))},
            xs::core::LogContextField{"lastHeartbeatAtUnixMs", ToString(entry.last_heartbeat_at_unix_ms)},
            xs::core::LogContextField{"elapsedMs", ToString(elapsed_ms)},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "GM marked an inner network session as timed out.",
            context);
    }

    if (cluster_nodes_online_needs_refresh)
    {
        RefreshClusterNodesOnlineState();
    }
}

} // namespace xs::node

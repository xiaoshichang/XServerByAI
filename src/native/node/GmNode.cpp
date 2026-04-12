#include "GmNode.h"

#include "BinarySerialization.h"
#include "InnerNetwork.h"
#include "Json.h"
#include "ManagedRuntimeHost.h"
#include "message/InnerClusterCodec.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RelayCodec.h"
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
inline constexpr std::string_view kUnknownServerEntityId = "unknown";
inline constexpr std::string_view kOnlineStubType = "OnlineStub";
inline constexpr std::uint32_t kOnlineStubBroadcastMsgId = 5201U;

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

bool ContainsNodeId(const std::vector<std::string>& node_ids, std::string_view node_id)
{
    return std::find(node_ids.begin(), node_ids.end(), node_id) != node_ids.end();
}

std::string DescribeManagedHostError(xs::host::ManagedHostErrorCode code)
{
    return std::string(xs::host::ManagedHostErrorCanonicalName(code)) + ": " +
           std::string(xs::host::ManagedHostErrorMessage(code));
}

xs::host::ManagedRuntimeHostOptions BuildManagedRuntimeHostOptions(const xs::core::ManagedConfig& managed_config)
{
    return xs::host::ManagedRuntimeHostOptions{
        .runtime_config_path = managed_config.runtime_config_path,
        .assembly_path = managed_config.assembly_path,
        .discovery_assembly_paths = managed_config.search_assembly_paths,
    };
}

bool TryReadManagedUtf8String(
    std::span<const std::uint8_t> utf8_buffer,
    std::uint32_t utf8_length,
    std::string* output)
{
    if (output == nullptr)
    {
        return false;
    }

    if (static_cast<std::size_t>(utf8_length) > utf8_buffer.size())
    {
        return false;
    }

    output->assign(
        reinterpret_cast<const char*>(utf8_buffer.data()),
        static_cast<std::size_t>(utf8_length));
    return true;
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
    InitializeStartupState();
    ResetServerStubStateTable();
    startup_state_.ready_epoch = 0U;

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
        return BuildControlHttpStatusSnapshot();
    };
    control_options.stop_handler = [this]() {
        RequestStop();
    };
    control_options.boardcase_handler = [this](std::string_view message) {
        return HandleBoardcaseRequest(message);
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

    startup_state_ = StartupState{};
    ResetServerStubStateTable();
    ClearError();
    return NodeErrorCode::None;
}

void GmNode::InitializeStartupState()
{
    startup_state_ = StartupState{};
    startup_state_.expected_gate_node_ids.reserve(cluster_config().gates.size());
    for (const auto& [node_id, config] : cluster_config().gates)
    {
        (void)config;
        startup_state_.expected_gate_node_ids.push_back(node_id);
    }

    startup_state_.expected_game_entries.reserve(cluster_config().games.size());
    for (const auto& [node_id, config] : cluster_config().games)
    {
        (void)config;
        startup_state_.expected_game_entries.push_back(
            GameMeshReadyEntry{
                .node_id = node_id,
            });
    }
}

void GmNode::ResetServerStubStateTable() noexcept
{
    server_stub_state_table_ = ServerStubStateTable{};
}

void GmNode::ResetServerStubStates() noexcept
{
    for (ServerStubEntry& entry : server_stub_state_table_.entries)
    {
        entry.entity_id = std::string(kUnknownServerEntityId);
        entry.state = ServerStubState::Init;
    }
}

bool GmNode::LoadManagedServerStubReflection()
{
    if (server_stub_state_table_.reflection_loaded)
    {
        return true;
    }

    if (server_stub_state_table_.reflection_load_failed)
    {
        return false;
    }

    const xs::core::ManagedConfig& managed_config = cluster_config().managed;
    const auto log_failure =
        [this, &managed_config](
            std::string_view message,
            std::string_view error_code,
            std::string_view error_detail,
            std::string_view entry_index = {},
            std::string_view export_result = {}) {
            server_stub_state_table_.entries.clear();
            server_stub_state_table_.reflection_loaded = false;
            server_stub_state_table_.reflection_load_failed = true;

            std::vector<xs::core::LogContextField> context;
            context.reserve(6);
            context.push_back(xs::core::LogContextField{"assemblyName", managed_config.assembly_name});
            context.push_back(xs::core::LogContextField{"assemblyPath", managed_config.assembly_path.string()});
            context.push_back(
                xs::core::LogContextField{"runtimeConfigPath", managed_config.runtime_config_path.string()});
            context.push_back(xs::core::LogContextField{"errorCode", std::string(error_code)});
            context.push_back(xs::core::LogContextField{"errorDetail", std::string(error_detail)});

            if (!entry_index.empty())
            {
                context.push_back(xs::core::LogContextField{"entryIndex", std::string(entry_index)});
            }

            if (!export_result.empty())
            {
                context.push_back(xs::core::LogContextField{"exportResult", std::string(export_result)});
            }

            logger().Log(xs::core::LogLevel::Error, "runtime", std::string(message), context);
            return false;
        };

    xs::host::ManagedRuntimeHost runtime_host;
    const xs::host::ManagedHostErrorCode load_result =
        runtime_host.Load(BuildManagedRuntimeHostOptions(managed_config));
    if (load_result != xs::host::ManagedHostErrorCode::None)
    {
        return log_failure(
            "GM failed to load managed server stub reflection runtime.",
            xs::host::ManagedHostErrorCanonicalName(load_result),
            DescribeManagedHostError(load_result));
    }

    const xs::host::ManagedHostErrorCode bind_result = runtime_host.BindExports();
    if (bind_result != xs::host::ManagedHostErrorCode::None)
    {
        return log_failure(
            "GM failed to bind managed exports.",
            xs::host::ManagedHostErrorCanonicalName(bind_result),
            DescribeManagedHostError(bind_result));
    }

    xs::host::ManagedExports reflection_exports{};
    const xs::host::ManagedHostErrorCode exports_result = runtime_host.GetExports(reflection_exports);
    if (exports_result != xs::host::ManagedHostErrorCode::None)
    {
        return log_failure(
            "GM failed to resolve managed exports.",
            xs::host::ManagedHostErrorCanonicalName(exports_result),
            DescribeManagedHostError(exports_result));
    }

    if (reflection_exports.get_server_stub_reflection_count == nullptr || reflection_exports.get_server_stub_reflection_entry == nullptr)
    {
        return log_failure(
            "GM resolved incomplete managed exports.",
            "Interop.InvalidReflectionExports",
            "Managed exports must provide both server stub reflection delegates.");
    }

    std::uint32_t reflection_count = 0U;
    const std::int32_t count_result = reflection_exports.get_server_stub_reflection_count(&reflection_count);
    if (count_result != 0)
    {
        return log_failure(
            "GM failed to read managed server stub reflection count.",
            "Interop.ManagedReflectionCountFailed",
            "Managed server stub reflection count export returned an error.",
            {},
            std::to_string(count_result));
    }

    if (reflection_count == 0U)
    {
        return log_failure(
            "GM loaded an empty managed server stub reflection.",
            "Interop.ManagedReflectionEmpty",
            "Managed server stub reflection must contain at least one entry.");
    }

    std::vector<ServerStubEntry> reflection_entries;
    reflection_entries.reserve(reflection_count);

    for (std::uint32_t index = 0U; index < reflection_count; ++index)
    {
        xs::host::ManagedServerStubReflectionEntry reflection_entry{};
        const std::int32_t entry_result = reflection_exports.get_server_stub_reflection_entry(index, &reflection_entry);
        if (entry_result != 0)
        {
            return log_failure(
                "GM failed to read a managed server stub reflection entry.",
                "Interop.ManagedReflectionEntryFailed",
                "Managed server stub reflection entry export returned an error.",
                std::to_string(index),
                std::to_string(entry_result));
        }

        ServerStubEntry definition{};
        const bool entity_type_ok = TryReadManagedUtf8String(
            std::span<const std::uint8_t>(
                reflection_entry.entity_type_utf8,
                std::size(reflection_entry.entity_type_utf8)),
            reflection_entry.entity_type_length,
            &definition.entity_type);
        const bool entity_id_ok = TryReadManagedUtf8String(
            std::span<const std::uint8_t>(
                reflection_entry.entity_id_utf8,
                std::size(reflection_entry.entity_id_utf8)),
            reflection_entry.entity_id_length,
            &definition.entity_id);
        if (!entity_type_ok || !entity_id_ok)
        {
            return log_failure(
                "GM received an invalid UTF-8 buffer description from the managed server stub reflection.",
                "Interop.ManagedReflectionEntryInvalid",
                "Managed server stub reflection entry lengths exceeded their declared native buffers.",
                std::to_string(index));
        }

        if (definition.entity_type.empty())
        {
            return log_failure(
                "GM received an empty managed server stub reflection entry.",
                "Interop.ManagedReflectionEntryInvalid",
                "Managed server stub reflection entries must provide a non-empty entityType value.",
                std::to_string(index));
        }

        if (definition.entity_id.empty())
        {
            definition.entity_id = std::string(kUnknownServerEntityId);
        }

        const auto duplicate_iterator = std::find_if(
            reflection_entries.begin(),
            reflection_entries.end(),
            [&definition](const ServerStubEntry& existing) {
                return existing.entity_type == definition.entity_type &&
                       existing.entity_id == definition.entity_id;
            });
        if (duplicate_iterator != reflection_entries.end())
        {
            return log_failure(
                "GM found a duplicate managed server stub reflection entry.",
                "Interop.ManagedReflectionDuplicate",
                definition.entity_type + "/" + definition.entity_id,
                std::to_string(index));
        }

        reflection_entries.push_back(std::move(definition));
    }

    server_stub_state_table_.entries = std::move(reflection_entries);
    server_stub_state_table_.reflection_loaded = true;
    server_stub_state_table_.reflection_load_failed = false;
    ResetServerStubStates();

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"assemblyName", managed_config.assembly_name},
        xs::core::LogContextField{"assemblyPath", managed_config.assembly_path.string()},
        xs::core::LogContextField{"runtimeConfigPath", managed_config.runtime_config_path.string()},
        xs::core::LogContextField{"entryCount", ToString(server_stub_state_table_.entries.size())},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "GM loaded managed server stub reflection.", context);
    return true;
}

bool GmNode::EnsureServerStubAssignments()
{
    if (!LoadManagedServerStubReflection())
    {
        return false;
    }

    if (server_stub_state_table_.entries.empty() || startup_state_.expected_game_entries.empty())
    {
        return false;
    }

    const bool all_assigned = std::all_of(
        server_stub_state_table_.entries.begin(),
        server_stub_state_table_.entries.end(),
        [](const ServerStubEntry& entry) {
            return !entry.owner_game_node_id.empty();
        });
    if (all_assigned)
    {
        return true;
    }

    std::uniform_int_distribution<std::size_t> distribution(
        0u,
        startup_state_.expected_game_entries.size() - 1u);
    std::mt19937_64 random_engine{std::random_device{}()};
    for (ServerStubEntry& entry : server_stub_state_table_.entries)
    {
        entry.owner_game_node_id = startup_state_.expected_game_entries[distribution(random_engine)].node_id;
        entry.state = ServerStubState::Init;
    }

    return true;
}

std::vector<xs::net::ServerStubOwnershipEntry> GmNode::BuildServerStubDistributeTable() const
{
    std::vector<xs::net::ServerStubOwnershipEntry> assignments;
    assignments.reserve(server_stub_state_table_.entries.size());

    for (const ServerStubEntry& entry : server_stub_state_table_.entries)
    {
        if (entry.owner_game_node_id.empty())
        {
            continue;
        }

        assignments.push_back(
            xs::net::ServerStubOwnershipEntry{
                .entity_type = entry.entity_type,
                .entity_id = entry.entity_id,
                .owner_game_node_id = entry.owner_game_node_id,
                .entry_flags = 0U,
            });
    }

    return assignments;
}

GmControlHttpStatusSnapshot GmNode::BuildControlHttpStatusSnapshot() const
{
    GmControlHttpStatusSnapshot snapshot;
    snapshot.inner_network_endpoint = inner_network() != nullptr ? std::string(inner_network()->bound_endpoint()) : "";
    snapshot.registered_process_count = static_cast<std::uint64_t>(inner_network_remote_sessions().size());
    snapshot.running = true;

    const std::vector<InnerNetworkSession> registry = inner_network_remote_sessions().Snapshot();
    const auto find_session = [&registry](std::string_view node_id) -> const InnerNetworkSession* {
        const auto iterator = std::find_if(
            registry.begin(),
            registry.end(),
            [node_id](const InnerNetworkSession& entry) {
                return entry.node_id == node_id;
            });
        return iterator != registry.end() ? &(*iterator) : nullptr;
    };

    snapshot.startup_flow.expected_game_count =
        static_cast<std::uint64_t>(startup_state_.expected_game_entries.size());
    snapshot.startup_flow.expected_gate_count =
        static_cast<std::uint64_t>(startup_state_.expected_gate_node_ids.size());
    snapshot.startup_flow.registered_game_count =
        static_cast<std::uint64_t>(startup_state_.registered_game_node_ids.size());
    snapshot.startup_flow.registered_gate_count =
        static_cast<std::uint64_t>(startup_state_.registered_gate_node_ids.size());
    snapshot.startup_flow.all_nodes_online = startup_state_.all_nodes_online;
    snapshot.startup_flow.last_all_nodes_online_server_now_unix_ms =
        startup_state_.last_all_nodes_online_server_now_unix_ms;
    snapshot.startup_flow.all_expected_games_mesh_ready =
        startup_state_.all_expected_games_mesh_ready;
    snapshot.startup_flow.reflection_loaded = server_stub_state_table_.reflection_loaded;
    snapshot.startup_flow.reflection_load_failed = server_stub_state_table_.reflection_load_failed;
    snapshot.startup_flow.total_stub_count = static_cast<std::uint64_t>(server_stub_state_table_.entries.size());

    const std::uint64_t assigned_stub_count = static_cast<std::uint64_t>(std::count_if(
        server_stub_state_table_.entries.begin(),
        server_stub_state_table_.entries.end(),
        [](const ServerStubEntry& entry) {
            return !entry.owner_game_node_id.empty();
        }));
    const std::uint64_t ready_stub_count = static_cast<std::uint64_t>(std::count_if(
        server_stub_state_table_.entries.begin(),
        server_stub_state_table_.entries.end(),
        [](const ServerStubEntry& entry) {
            return !entry.owner_game_node_id.empty() && entry.state == ServerStubState::Ready;
        }));
    snapshot.startup_flow.assigned_stub_count = assigned_stub_count;
    snapshot.startup_flow.ready_stub_count = ready_stub_count;
    snapshot.startup_flow.ownership_active =
        snapshot.startup_flow.total_stub_count > 0U &&
        assigned_stub_count == snapshot.startup_flow.total_stub_count;
    snapshot.startup_flow.assignment_epoch =
        snapshot.startup_flow.ownership_active ? kServerStubOwnershipAssignmentEpoch : 0U;
    snapshot.startup_flow.ready_epoch = startup_state_.ready_epoch;
    snapshot.startup_flow.cluster_ready = startup_state_.gate_open;
    snapshot.startup_flow.last_cluster_ready_server_now_unix_ms =
        startup_state_.last_cluster_ready_server_now_unix_ms;

    snapshot.nodes.reserve(
        startup_state_.expected_game_entries.size() +
        startup_state_.expected_gate_node_ids.size());
    const auto append_expected_nodes =
        [&snapshot, &find_session](const std::vector<std::string>& node_ids, xs::core::ProcessType process_type) {
            for (const std::string& node_id : node_ids)
            {
                const InnerNetworkSession* session = find_session(node_id);

                GmControlHttpStatusSnapshot::NodeSnapshot node_snapshot;
                node_snapshot.node_id = node_id;
                node_snapshot.process_type = std::string(xs::core::ProcessTypeName(process_type));
                if (session != nullptr)
                {
                    node_snapshot.pid = session->pid;
                    node_snapshot.inner_network_endpoint = BuildInnerNetworkEndpointText(session->inner_network_endpoint);
                    node_snapshot.last_heartbeat_at_unix_ms = session->last_heartbeat_at_unix_ms;
                    node_snapshot.last_server_now_unix_ms = session->last_server_now_unix_ms;
                    node_snapshot.last_protocol_error = session->last_protocol_error;
                    node_snapshot.registered = session->registered;
                    node_snapshot.heartbeat_timed_out = session->heartbeat_timed_out;
                    node_snapshot.online = session->registered && !session->heartbeat_timed_out;
                    node_snapshot.inner_network_ready = session->inner_network_ready;
                }

                snapshot.nodes.push_back(std::move(node_snapshot));
            }
        };
    std::vector<std::string> expected_game_node_ids;
    expected_game_node_ids.reserve(startup_state_.expected_game_entries.size());
    for (const GameMeshReadyEntry& entry : startup_state_.expected_game_entries)
    {
        expected_game_node_ids.push_back(entry.node_id);
    }
    append_expected_nodes(expected_game_node_ids, xs::core::ProcessType::Game);
    append_expected_nodes(startup_state_.expected_gate_node_ids, xs::core::ProcessType::Gate);

    snapshot.game_mesh_ready.reserve(startup_state_.expected_game_entries.size());
    for (const GameMeshReadyEntry& entry : startup_state_.expected_game_entries)
    {
        snapshot.game_mesh_ready.push_back(
            GmControlHttpStatusSnapshot::GameMeshReadySnapshot{
                .node_id = entry.node_id,
                .mesh_ready = entry.mesh_ready,
                .reported_at_unix_ms = entry.reported_at_unix_ms,
            });
    }

    snapshot.stub_distribution.reserve(startup_state_.expected_game_entries.size());
    for (const GameMeshReadyEntry& game_entry : startup_state_.expected_game_entries)
    {
        GmControlHttpStatusSnapshot::StubOwnerSnapshot owner_snapshot;
        owner_snapshot.node_id = game_entry.node_id;

        for (const ServerStubEntry& entry : server_stub_state_table_.entries)
        {
            if (entry.owner_game_node_id != game_entry.node_id)
            {
                continue;
            }

            ++owner_snapshot.owned_stub_count;
            if (entry.state == ServerStubState::Ready)
            {
                ++owner_snapshot.ready_stub_count;
            }

            owner_snapshot.stubs.push_back(
                GmControlHttpStatusSnapshot::StubSnapshot{
                    .entity_type = entry.entity_type,
                    .entity_id = entry.entity_id,
                    .state = entry.state == ServerStubState::Ready ? "Ready" : "Init",
                });
        }

        snapshot.stub_distribution.push_back(std::move(owner_snapshot));
    }

    return snapshot;
}

GmControlHttpResponse GmNode::HandleBoardcaseRequest(std::string_view message)
{
    if (message.empty())
    {
        xs::core::Json body{
            {"error", "Broadcast message must not be empty."},
        };

        GmControlHttpResponse response;
        response.status_code = 400;
        response.body = body.dump();
        return response;
    }

    const ServerStubEntry* online_stub_entry = server_stub_entry(kOnlineStubType);
    if (online_stub_entry == nullptr ||
        online_stub_entry->owner_game_node_id.empty() ||
        online_stub_entry->state != ServerStubState::Ready)
    {
        xs::core::Json body{
            {"error", "OnlineStub owner is not available."},
            {"stubType", std::string(kOnlineStubType)},
        };

        GmControlHttpResponse response;
        response.status_code = 503;
        response.body = body.dump();
        return response;
    }

    const std::span<const std::byte> payload(
        reinterpret_cast<const std::byte*>(message.data()),
        message.size());
    std::string error_message;
    if (!TrySendStubCallToGame(
            online_stub_entry->owner_game_node_id,
            kOnlineStubType,
            online_stub_entry->entity_id == kUnknownServerEntityId ? std::string_view{} : std::string_view(online_stub_entry->entity_id),
            kOnlineStubBroadcastMsgId,
            payload,
            &error_message))
    {
        xs::core::Json body{
            {"error", error_message.empty() ? "Failed to forward boardcase message to OnlineStub owner." : error_message},
            {"stubType", std::string(kOnlineStubType)},
            {"ownerGameNodeId", online_stub_entry->owner_game_node_id},
        };

        GmControlHttpResponse response;
        response.status_code = 503;
        response.body = body.dump();
        return response;
    }

    const std::array<xs::core::LogContextField, 3> context{
        xs::core::LogContextField{"stubType", std::string(kOnlineStubType)},
        xs::core::LogContextField{"ownerGameNodeId", online_stub_entry->owner_game_node_id},
        xs::core::LogContextField{"messageBytes", ToString(message.size())},
    };
    logger().Log(xs::core::LogLevel::Info, "control.http", "GM forwarded /boardcase request to OnlineStub owner.", context);

    xs::core::Json body{
        {"status", "forwarded"},
        {"stubType", std::string(kOnlineStubType)},
        {"ownerGameNodeId", online_stub_entry->owner_game_node_id},
        {"message", std::string(message)},
    };

    GmControlHttpResponse response;
    response.status_code = 200;
    response.body = body.dump();
    return response;
}

GmNode::GameMeshReadyEntry* GmNode::mesh_ready_entry(std::string_view node_id) noexcept
{
    auto iterator = std::find_if(
        startup_state_.expected_game_entries.begin(),
        startup_state_.expected_game_entries.end(),
        [node_id](const GameMeshReadyEntry& entry) {
            return entry.node_id == node_id;
        });
    return iterator != startup_state_.expected_game_entries.end() ? &(*iterator) : nullptr;
}

const GmNode::GameMeshReadyEntry* GmNode::mesh_ready_entry(std::string_view node_id) const noexcept
{
    auto iterator = std::find_if(
        startup_state_.expected_game_entries.begin(),
        startup_state_.expected_game_entries.end(),
        [node_id](const GameMeshReadyEntry& entry) {
            return entry.node_id == node_id;
        });
    return iterator != startup_state_.expected_game_entries.end() ? &(*iterator) : nullptr;
}

const GmNode::ServerStubEntry* GmNode::server_stub_entry(std::string_view entity_type) const noexcept
{
    const auto iterator = std::find_if(
        server_stub_state_table_.entries.begin(),
        server_stub_state_table_.entries.end(),
        [entity_type](const ServerStubEntry& entry) {
            return entry.entity_type == entity_type;
        });
    return iterator != server_stub_state_table_.entries.end() ? &(*iterator) : nullptr;
}

bool GmNode::TrySendStubCallToGame(
    std::string_view target_game_node_id,
    std::string_view target_stub_type,
    std::string_view target_entity_id,
    std::uint32_t msg_id,
    std::span<const std::byte> payload,
    std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }

    if (target_game_node_id.empty() || target_stub_type.empty() || msg_id == 0U)
    {
        if (error_message != nullptr)
        {
            *error_message = "GM stub relay arguments are invalid.";
        }
        return false;
    }

    if (inner_network() == nullptr)
    {
        if (error_message != nullptr)
        {
            *error_message = "GM inner network is unavailable.";
        }
        return false;
    }

    InnerNetworkSession* target_session = inner_network_remote_sessions().FindMutableByNodeId(target_game_node_id);
    if (target_session == nullptr ||
        target_session->process_type != xs::core::ProcessType::Game ||
        target_session->connection_state != ipc::ZmqConnectionState::Connected ||
        !target_session->registered ||
        !target_session->inner_network_ready ||
        target_session->routing_id.empty())
    {
        if (error_message != nullptr)
        {
            *error_message = "OnlineStub owner Game is unavailable on the inner network.";
        }
        return false;
    }

    const xs::net::RelayForwardMailboxCall relay_message{
        .source_game_node_id = std::string(node_id()),
        .target_game_node_id = std::string(target_game_node_id),
        .target_entity_id = std::string(target_entity_id),
        .target_mailbox_name = std::string(target_stub_type),
        .mailbox_call_msg_id = msg_id,
        .relay_flags = 0u,
        .payload = std::vector<std::byte>(payload.begin(), payload.end()),
    };

    std::size_t wire_size = 0u;
    const xs::net::RelayCodecErrorCode wire_size_result =
        xs::net::GetRelayForwardMailboxCallWireSize(relay_message, &wire_size);
    if (wire_size_result != xs::net::RelayCodecErrorCode::None)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string(xs::net::RelayCodecErrorMessage(wire_size_result));
        }
        return false;
    }

    std::vector<std::byte> relay_payload(wire_size);
    const xs::net::RelayCodecErrorCode encode_result =
        xs::net::EncodeRelayForwardMailboxCall(relay_message, relay_payload);
    if (encode_result != xs::net::RelayCodecErrorCode::None)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string(xs::net::RelayCodecErrorMessage(encode_result));
        }
        return false;
    }

    const xs::net::PacketHeader header =
        xs::net::MakePacketHeader(xs::net::kRelayForwardMailboxCallMsgId, xs::net::kPacketSeqNone, 0U,
                                  static_cast<std::uint32_t>(relay_payload.size()));
    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + relay_payload.size());
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, relay_payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        }
        return false;
    }

    const NodeErrorCode send_result = inner_network()->Send(target_session->routing_id, packet);
    if (send_result != NodeErrorCode::None)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string(inner_network()->last_error_message());
        }
        return false;
    }

    return true;
}

bool GmNode::AreAllExpectedNodesOnline() const noexcept
{
    const std::size_t expected_node_count =
        startup_state_.expected_gate_node_ids.size() +
        startup_state_.expected_game_entries.size();
    if (expected_node_count == 0U)
    {
        return false;
    }

    return startup_state_.registered_gate_node_ids.size() == startup_state_.expected_gate_node_ids.size() &&
           startup_state_.registered_game_node_ids.size() == startup_state_.expected_game_entries.size();
}

bool GmNode::AreAllExpectedGamesMeshReady() const noexcept
{
    if (startup_state_.expected_game_entries.empty())
    {
        return false;
    }

    return std::all_of(
        startup_state_.expected_game_entries.begin(),
        startup_state_.expected_game_entries.end(),
        [](const GameMeshReadyEntry& entry) {
            return entry.mesh_ready;
        });
}

bool GmNode::AreAllServerStubsReady() const noexcept
{
    if (server_stub_state_table_.entries.empty())
    {
        return false;
    }

    return std::all_of(
        server_stub_state_table_.entries.begin(),
        server_stub_state_table_.entries.end(),
        [](const ServerStubEntry& entry) {
            return !entry.owner_game_node_id.empty() && entry.state == ServerStubState::Ready;
        });
}

void GmNode::RecordStartupRegistration(xs::core::ProcessType process_type, std::string_view node_id)
{
    if (process_type == xs::core::ProcessType::Game)
    {
        if (mesh_ready_entry(node_id) == nullptr || ContainsNodeId(startup_state_.registered_game_node_ids, node_id))
        {
            return;
        }

        startup_state_.registered_game_node_ids.push_back(std::string(node_id));
        return;
    }

    if (process_type != xs::core::ProcessType::Gate ||
        !ContainsNodeId(startup_state_.expected_gate_node_ids, node_id) ||
        ContainsNodeId(startup_state_.registered_gate_node_ids, node_id))
    {
        return;
    }

    startup_state_.registered_gate_node_ids.push_back(std::string(node_id));
}

void GmNode::OnAllNodeOnline()
{
    if (startup_state_.all_nodes_online)
    {
        return;
    }

    const std::uint64_t server_now_unix_ms = CurrentUnixTimeMilliseconds();
    startup_state_.all_nodes_online = true;
    startup_state_.last_all_nodes_online_server_now_unix_ms = server_now_unix_ms;

    std::uint64_t notify_target_count = 0U;
    for (const GameMeshReadyEntry& game_entry : startup_state_.expected_game_entries)
    {
        const InnerNetworkSession* session = inner_network_remote_sessions().FindByNodeId(game_entry.node_id);
        if (session == nullptr || !session->registered || session->heartbeat_timed_out)
        {
            continue;
        }

        ++notify_target_count;
        SendClusterNodesOnlineNotifyToGame(*session, true, server_now_unix_ms);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"allNodesOnline", "true"},
        xs::core::LogContextField{"notifyTargetCount", ToString(notify_target_count)},
        xs::core::LogContextField{"expectedGameCount", ToString(startup_state_.expected_game_entries.size())},
        xs::core::LogContextField{"expectedGateCount", ToString(startup_state_.expected_gate_node_ids.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM reached the all-nodes-online startup node.", context);
}

void GmNode::OnAllGameReady()
{
    if (startup_state_.all_expected_games_mesh_ready)
    {
        return;
    }

    startup_state_.all_expected_games_mesh_ready = true;
    if (!EnsureServerStubAssignments())
    {
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"expectedGameCount", ToString(startup_state_.expected_game_entries.size())},
            xs::core::LogContextField{"reflectionEntryCount", ToString(server_stub_state_table_.entries.size())},
        };
        logger().Log(
            xs::core::LogLevel::Error,
            "inner",
            "GM failed to assign server stubs after all Games reported mesh ready.",
            context);
        return;
    }

    ResetServerStubStates();

    const std::vector<xs::net::ServerStubOwnershipEntry> assignments = BuildServerStubDistributeTable();
    if (assignments.empty())
    {
        const std::array<xs::core::LogContextField, 1> context{
            xs::core::LogContextField{"expectedGameCount", ToString(startup_state_.expected_game_entries.size())},
        };
        logger().Log(
            xs::core::LogLevel::Error,
            "inner",
            "GM produced no ownership assignments after mesh-ready aggregation completed.",
            context);
        return;
    }

    const std::uint64_t server_now_unix_ms = CurrentUnixTimeMilliseconds();

    const xs::net::ServerStubOwnershipSync sync
    {
        .assignment_epoch = kServerStubOwnershipAssignmentEpoch,
        .status_flags = 0U,
        .assignments = assignments,
        .server_now_unix_ms = server_now_unix_ms,
    };

    std::uint64_t notify_target_count = 0U;
    for (const GameMeshReadyEntry& game_entry : startup_state_.expected_game_entries)
    {
        const InnerNetworkSession* session = inner_network_remote_sessions().FindByNodeId(game_entry.node_id);
        if (session == nullptr || !session->registered || session->heartbeat_timed_out)
        {
            continue;
        }

        ++notify_target_count;
        SendOwnershipSyncToGame(*session, sync);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"assignmentEpoch", ToString(kServerStubOwnershipAssignmentEpoch)},
        xs::core::LogContextField{"assignmentCount", ToString(assignments.size())},
        xs::core::LogContextField{"notifyTargetCount", ToString(notify_target_count)},
        xs::core::LogContextField{"expectedGameCount", ToString(startup_state_.expected_game_entries.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM reached the all-games-ready startup node.", context);
}

void GmNode::OnAllStubReady()
{
    if (startup_state_.gate_open || !AreAllServerStubsReady())
    {
        return;
    }

    const std::uint64_t server_now_unix_ms = CurrentUnixTimeMilliseconds();
    startup_state_.gate_open = true;
    startup_state_.last_cluster_ready_server_now_unix_ms = server_now_unix_ms;
    ++startup_state_.ready_epoch;
    const xs::net::ClusterReadyNotify notify{
        .ready_epoch = startup_state_.ready_epoch,
        .cluster_ready = true,
        .status_flags = 0U,
        .server_now_unix_ms = server_now_unix_ms,
    };

    std::uint64_t notify_target_count = 0U;
    for (const std::string& node_id : startup_state_.expected_gate_node_ids)
    {
        const InnerNetworkSession* session = inner_network_remote_sessions().FindByNodeId(node_id);
        if (session == nullptr || !session->registered || session->heartbeat_timed_out)
        {
            continue;
        }

        ++notify_target_count;
        SendClusterGateOpenNotifyToGate(*session, notify);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"readyEpoch", ToString(notify.ready_epoch)},
        xs::core::LogContextField{"clusterReady", notify.cluster_ready ? "true" : "false"},
        xs::core::LogContextField{"notifyTargetCount", ToString(notify_target_count)},
        xs::core::LogContextField{"assignmentCount", ToString(server_stub_state_table_.entries.size())},
        xs::core::LogContextField{"serverNowUnixMs", ToString(server_now_unix_ms)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM reached the all-stubs-ready startup node.", context);
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

void GmNode::SendClusterGateOpenNotifyToGate(
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
        HandleGameStubsReadyReport(routing_id, payload);
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
        send_error_response(kInnerNodeIdConflict, &request);
        return;
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

    if (session == nullptr)
    {
        std::vector<xs::core::LogContextField> context = BuildRegisterContext(routing_id, packet.header.seq, &request);
        logger().Log(xs::core::LogLevel::Error, "inner", "GM could not load the registered session after Register().", context);
        return;
    }
    apply_registration_state(*session);

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

    RecordStartupRegistration(registration.process_type, request.node_id);
    if (!startup_state_.all_nodes_online && AreAllExpectedNodesOnline())
    {
        OnAllNodeOnline();
    }
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

    if (!session->registered || session->heartbeat_timed_out)
    {
        log_heartbeat_rejected(
            kInnerNodeNotRegistered,
            "Inner.NodeNotRegistered",
            "GM inner service ignored heartbeat from a session outside the startup happy path.");
        return;
    }

    session->load = request.load;
    session->last_heartbeat_at_unix_ms = now_unix_ms;
    session->inner_network_ready = true;
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

    logger().Log(xs::core::LogLevel::Debug, "inner", "GM inner service refreshed heartbeat state.", context);
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
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored mesh ready report from an unregistered Game session.", context);
        return;
    }

    if (!startup_state_.all_nodes_online)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"nodeId", session->node_id});
        logger().Log(xs::core::LogLevel::Error, "inner", "GM rejected mesh ready report before all expected nodes registered.", context);
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

    entry->mesh_ready = true;
    entry->reported_at_unix_ms = report.reported_at_unix_ms;

    const std::array<xs::core::LogContextField, 3> context{
        xs::core::LogContextField{"nodeId", session->node_id},
        xs::core::LogContextField{"reportedAtUnixMs", ToString(report.reported_at_unix_ms)},
        xs::core::LogContextField{"allNodesOnline", startup_state_.all_nodes_online ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM accepted mesh ready report.", context);

    if (!startup_state_.all_expected_games_mesh_ready && AreAllExpectedGamesMeshReady())
    {
        OnAllGameReady();
    }
}

void GmNode::HandleGameStubsReadyReport(
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

    const bool ownership_active = !server_stub_state_table_.entries.empty() &&
                                  std::all_of(
                                      server_stub_state_table_.entries.begin(),
                                      server_stub_state_table_.entries.end(),
                                      [](const ServerStubEntry& entry) {
                                          return !entry.owner_game_node_id.empty();
                                      });
    if (!startup_state_.all_expected_games_mesh_ready || !ownership_active)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"nodeId", session->node_id});
        context.push_back(xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)});
        logger().Log(xs::core::LogLevel::Error, "inner", "GM rejected service ready report before ownership sync became active.", context);
        return;
    }

    if (report.assignment_epoch != kServerStubOwnershipAssignmentEpoch)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", session->node_id},
            xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
            xs::core::LogContextField{"expectedAssignmentEpoch", ToString(kServerStubOwnershipAssignmentEpoch)},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM rejected stale service ready report.", context);
        return;
    }

    if (!report.local_ready)
    {
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"nodeId", session->node_id},
            xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "GM rejected a non-happy-path service ready report.", context);
        return;
    }

    std::vector<ServerStubEntry*> owned_entries;
    owned_entries.reserve(server_stub_state_table_.entries.size());
    for (ServerStubEntry& entry : server_stub_state_table_.entries)
    {
        if (entry.owner_game_node_id != session->node_id)
        {
            continue;
        }

        owned_entries.push_back(&entry);
    }

    if (owned_entries.empty())
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(xs::core::LogContextField{"nodeId", session->node_id});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM ignored service ready report from a Game node without owned stubs.", context);
        return;
    }

    std::size_t matched_ready_count = 0U;
    std::size_t ignored_ready_count = 0U;
    for (const xs::net::ServerStubReadyEntry& ready_entry : report.entries)
    {
        if (!ready_entry.ready)
        {
            ++ignored_ready_count;
            continue;
        }

        if (ready_entry.entity_id.empty() || ready_entry.entity_id == kUnknownServerEntityId)
        {
            ++ignored_ready_count;
            continue;
        }

        const auto iterator = std::find_if(
            owned_entries.begin(),
            owned_entries.end(),
            [&ready_entry](const ServerStubEntry* entry) {
                return entry != nullptr &&
                       entry->entity_type == ready_entry.entity_type &&
                       (entry->entity_id == kUnknownServerEntityId ||
                        entry->entity_id == ready_entry.entity_id);
            });
        if (iterator == owned_entries.end())
        {
            ++ignored_ready_count;
            continue;
        }

        (*iterator)->entity_id = ready_entry.entity_id;
        (*iterator)->state = ServerStubState::Ready;
        ++matched_ready_count;
    }

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"nodeId", session->node_id},
        xs::core::LogContextField{"assignmentEpoch", ToString(report.assignment_epoch)},
        xs::core::LogContextField{"localReady", report.local_ready ? "true" : "false"},
        xs::core::LogContextField{"ownedStubCount", ToString(owned_entries.size())},
        xs::core::LogContextField{"readyEntryCount", ToString(report.entries.size())},
        xs::core::LogContextField{"matchedReadyCount", ToString(matched_ready_count)},
        xs::core::LogContextField{"ignoredReadyCount", ToString(ignored_ready_count)},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "GM accepted service ready report.", context);

    if (!startup_state_.gate_open && AreAllServerStubsReady())
    {
        OnAllStubReady();
    }
}

void GmNode::HandleTimeoutScan()
{
    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();

    const std::vector<InnerNetworkSession> snapshot = inner_network_remote_sessions().Snapshot();
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

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", entry.node_id},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(entry.routing_id.size()))},
            xs::core::LogContextField{"lastHeartbeatAtUnixMs", ToString(entry.last_heartbeat_at_unix_ms)},
            xs::core::LogContextField{"elapsedMs", ToString(elapsed_ms)},
        };
        logger().Log(
            xs::core::LogLevel::Error,
            "inner",
            "GM detected a heartbeat timeout outside the startup happy path.",
            context);
    }
}

} // namespace xs::node

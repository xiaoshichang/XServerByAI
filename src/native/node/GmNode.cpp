#include "GmNode.h"

#include "BinarySerialization.h"
#include "InnerNetwork.h"
#include "TimeUtils.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <array>
#include <chrono>
#include <limits>
#include <optional>
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

inline constexpr std::int32_t kInnerProcessTypeInvalid = 3000;
inline constexpr std::int32_t kInnerNodeIdConflict = 3001;
inline constexpr std::int32_t kInnerNodeNotRegistered = 3003;
inline constexpr std::int32_t kInnerNetworkEndpointInvalid = 3002;
inline constexpr std::int32_t kInnerChannelInvalid = 3004;
inline constexpr std::int32_t kInnerRequestInvalid = 3005;

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

std::uint64_t CurrentServerNowUnixMs() noexcept
{
    const std::int64_t now = xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow());
    return now < 0 ? 0u : static_cast<std::uint64_t>(now);
}

std::string MakeRoutingKey(std::span<const std::byte> routing_id)
{
    return std::string(
        reinterpret_cast<const char*>(routing_id.data()),
        reinterpret_cast<const char*>(routing_id.data() + routing_id.size()));
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

std::uint16_t HeartbeatResponseFlags(bool is_error) noexcept
{
    std::uint16_t flags = static_cast<std::uint16_t>(xs::net::PacketFlag::Response);
    if (is_error)
    {
        flags |= static_cast<std::uint16_t>(xs::net::PacketFlag::Error);
    }

    return flags;
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
    options.mode = InnerNetworkMode::PassiveListener;
    options.local_endpoint = BuildTcpEndpoint(endpoint);

    invalidated_routing_ids_.clear();
    timeout_scan_timer_id_ = 0;
    inner_network_remote_sessions().Clear();

    const NodeErrorCode init_result = InitInnerNetwork(std::move(options));
    if (init_result != NodeErrorCode::None)
    {
        return init_result;
    }

    inner_network()->SetMessageHandler([this](std::span<const std::byte> routing_id, std::span<const std::byte> payload) {
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

    invalidated_routing_ids_.clear();

    ClearError();
    return NodeErrorCode::None;
}

void GmNode::HandleInnerMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    xs::net::PacketHeader raw_header{};
    if (!TryReadRawPacketHeader(payload, &raw_header))
    {
        HandleHeartbeatMessage(routing_id, payload);
        return;
    }

    if (raw_header.msg_id == xs::net::kInnerHeartbeatMsgId)
    {
        HandleHeartbeatMessage(routing_id, payload);
        return;
    }

    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        std::vector<xs::core::LogContextField> context = BuildPacketContext(routing_id, payload.size());
        context.push_back(
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))});
        logger().Log(xs::core::LogLevel::Warn, "inner", "GM dropped malformed inner packet.", context);
        return;
    }

    if (packet.header.msg_id != xs::net::kInnerRegisterMsgId)
    {
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

    const std::uint64_t server_now_unix_ms = CurrentServerNowUnixMs();
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

    const InnerNetworkSessionManagerErrorCode register_result =
        inner_network_remote_sessions().Register(std::move(registration));
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
            std::vector<xs::core::LogContextField> context = BuildRegisterContext(routing_id, packet.header.seq, &request);
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

    invalidated_routing_ids_.erase(MakeRoutingKey(routing_id));
    InnerNetworkSession* session = inner_network_remote_sessions().FindMutableByNodeId(request.node_id);
    if (session != nullptr)
    {
        session->registered = true;
        session->connection_state = ipc::ZmqConnectionState::Connected;
        session->heartbeat_interval_ms = kDefaultHeartbeatIntervalMs;
        session->heartbeat_timeout_ms = kDefaultHeartbeatTimeoutMs;
        session->last_server_now_unix_ms = server_now_unix_ms;
        session->last_protocol_error.clear();
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
    PruneExpiredInvalidatedRoutingIds(now_unix_ms);

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

    auto send_heartbeat_error = [this, routing_id, &raw_header](
                                    std::int32_t error_code,
                                    std::string_view error_name,
                                    bool require_full_register,
                                    std::string_view log_message) {
        const xs::net::HeartbeatErrorResponse response{
            .error_code = error_code,
            .retry_after_ms = 0U,
            .require_full_register = require_full_register,
        };
        std::array<std::byte, xs::net::kHeartbeatErrorResponseSize> response_body{};
        if (xs::net::EncodeHeartbeatErrorResponse(response, response_body) != xs::net::HeartbeatCodecErrorCode::None)
        {
            return;
        }

        const xs::net::PacketHeader response_header = xs::net::MakePacketHeader(
            xs::net::kInnerHeartbeatMsgId,
            raw_header.seq,
            HeartbeatResponseFlags(true),
            static_cast<std::uint32_t>(response_body.size()));
        std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kHeartbeatErrorResponseSize> response_packet{};
        if (xs::net::EncodePacket(response_header, response_body, response_packet) != xs::net::PacketCodecErrorCode::None)
        {
            return;
        }

        if (inner_network() == nullptr)
        {
            return;
        }

        const NodeErrorCode send_result = inner_network()->Send(routing_id, response_packet);
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"seq", ToString(static_cast<std::uint64_t>(raw_header.seq))},
        };
        if (send_result != NodeErrorCode::None)
        {
            logger().Log(
                xs::core::LogLevel::Warn,
                "inner",
                "GM inner service failed to send heartbeat error response.",
                context,
                error_code,
                error_name);
            return;
        }

        logger().Log(xs::core::LogLevel::Warn, "inner", log_message, context, error_code, error_name);
    };

    if (!xs::net::IsValidPacketFlags(raw_header.flags) ||
        raw_header.flags != 0U ||
        raw_header.seq == xs::net::kPacketSeqNone)
    {
        send_heartbeat_error(
            kInnerRequestInvalid,
            "Inner.RequestInvalid",
            false,
            "GM inner service rejected an invalid heartbeat request.");
        return;
    }

    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode decode_packet_result = xs::net::DecodePacket(payload, &packet);
    if (decode_packet_result != xs::net::PacketCodecErrorCode::None)
    {
        send_heartbeat_error(
            kInnerRequestInvalid,
            "Inner.RequestInvalid",
            false,
            "GM inner service rejected a malformed heartbeat packet.");
        return;
    }

    xs::net::HeartbeatRequest request{};
    const xs::net::HeartbeatCodecErrorCode decode_request_result =
        xs::net::DecodeHeartbeatRequest(packet.payload, &request);
    if (decode_request_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        send_heartbeat_error(
            kInnerRequestInvalid,
            "Inner.RequestInvalid",
            false,
            "GM inner service rejected a malformed heartbeat payload.");
        return;
    }

    InnerNetworkSession* session = inner_network_remote_sessions().FindMutableByRoutingId(routing_id);
    if (session == nullptr)
    {
        const bool invalidated = ContainsInvalidatedRoutingId(routing_id);
        send_heartbeat_error(
            invalidated ? kInnerChannelInvalid : kInnerNodeNotRegistered,
            invalidated ? "Inner.ChannelInvalid" : "Inner.NodeNotRegistered",
            true,
            invalidated
                ? "GM inner service rejected heartbeat on an invalidated inner channel."
                : "GM inner service rejected heartbeat from an unknown inner channel.");
        return;
    }

    session->load = request.load;
    session->last_heartbeat_at_unix_ms = now_unix_ms;
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
        HeartbeatResponseFlags(false),
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
}

void GmNode::HandleTimeoutScan()
{
    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();
    PruneExpiredInvalidatedRoutingIds(now_unix_ms);

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

        const InnerNetworkSessionManagerErrorCode unregister_result =
            inner_network_remote_sessions().UnregisterByNodeId(entry.node_id);
        if (unregister_result != InnerNetworkSessionManagerErrorCode::None)
        {
            continue;
        }

        RememberInvalidatedRoutingId(entry.routing_id, now_unix_ms);
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", entry.node_id},
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(entry.routing_id.size()))},
            xs::core::LogContextField{"lastHeartbeatAtUnixMs", ToString(entry.last_heartbeat_at_unix_ms)},
            xs::core::LogContextField{"elapsedMs", ToString(elapsed_ms)},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "GM inner service evicted a timed-out inner network session.",
            context,
            kInnerChannelInvalid,
            "Inner.ChannelInvalid");
    }
}

void GmNode::RememberInvalidatedRoutingId(
    std::span<const std::byte> routing_id,
    std::uint64_t now_unix_ms)
{
    if (routing_id.empty())
    {
        return;
    }

    const std::uint64_t retention_ms = InvalidatedRoutingRetentionMs();
    const std::uint64_t expires_at_unix_ms =
        retention_ms > std::numeric_limits<std::uint64_t>::max() - now_unix_ms
            ? std::numeric_limits<std::uint64_t>::max()
            : now_unix_ms + retention_ms;

    invalidated_routing_ids_[MakeRoutingKey(routing_id)] = expires_at_unix_ms;
}

void GmNode::PruneExpiredInvalidatedRoutingIds(std::uint64_t now_unix_ms)
{
    for (auto iterator = invalidated_routing_ids_.begin(); iterator != invalidated_routing_ids_.end();)
    {
        if (iterator->second <= now_unix_ms)
        {
            iterator = invalidated_routing_ids_.erase(iterator);
            continue;
        }

        ++iterator;
    }
}

bool GmNode::ContainsInvalidatedRoutingId(std::span<const std::byte> routing_id) const
{
    if (routing_id.empty())
    {
        return false;
    }

    const auto iterator = invalidated_routing_ids_.find(MakeRoutingKey(routing_id));
    if (iterator == invalidated_routing_ids_.end())
    {
        return false;
    }

    return iterator->second > CurrentUnixTimeMilliseconds();
}

std::uint64_t GmNode::CurrentUnixTimeMilliseconds() const noexcept
{
    return CurrentServerNowUnixMs();
}

std::uint64_t GmNode::InvalidatedRoutingRetentionMs() const noexcept
{
    return static_cast<std::uint64_t>(kDefaultHeartbeatTimeoutMs);
}

} // namespace xs::node

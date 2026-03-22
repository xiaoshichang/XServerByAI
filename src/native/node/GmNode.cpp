#include "GmNode.h"

#include "BinarySerialization.h"
#include "TimeUtils.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <array>
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

inline constexpr std::int32_t kInnerProcessTypeInvalid = 3000;
inline constexpr std::int32_t kInnerNodeIdConflict = 3001;
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

std::uint64_t CurrentServerNowUnixMs() noexcept
{
    const std::int64_t now = xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow());
    return now < 0 ? 0u : static_cast<std::uint64_t>(now);
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

std::optional<std::int32_t> MapProcessRegistryErrorToInnerError(ProcessRegistryErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case ProcessRegistryErrorCode::None:
        return std::nullopt;
    case ProcessRegistryErrorCode::InvalidProcessType:
        return kInnerProcessTypeInvalid;
    case ProcessRegistryErrorCode::InvalidInnerNetworkEndpointHost:
    case ProcessRegistryErrorCode::InvalidInnerNetworkEndpointPort:
        return kInnerNetworkEndpointInvalid;
    case ProcessRegistryErrorCode::NodeIdConflict:
        return kInnerNodeIdConflict;
    case ProcessRegistryErrorCode::RoutingIdConflict:
    case ProcessRegistryErrorCode::NodeNotFound:
    case ProcessRegistryErrorCode::RoutingIdNotFound:
        return kInnerChannelInvalid;
    case ProcessRegistryErrorCode::InvalidArgument:
    case ProcessRegistryErrorCode::InvalidNodeId:
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

std::vector<ProcessRegistryEntry> GmNode::registry_snapshot() const
{
    if (inner_service_ == nullptr)
    {
        return {};
    }

    return inner_service_->process_registry().Snapshot();
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

    InnerNetworkOptions options;
    options.mode = InnerNetworkMode::PassiveListener;
    options.local_endpoint = BuildTcpEndpoint(endpoint);

    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), std::move(options));

    const NodeErrorCode init_result = inner_network_->Init();
    if (init_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        return SetError(init_result, error_message);
    }

    inner_service_ = std::make_unique<GmInnerService>(event_loop(), logger(), *inner_network_);
    const NodeErrorCode inner_init_result = inner_service_->Init();
    if (inner_init_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(inner_service_->last_error_message());
        inner_service_.reset();
        (void)inner_network_->Uninit();
        inner_network_.reset();
        return SetError(inner_init_result, error_message);
    }

    inner_network_->SetMessageHandler([this](std::span<const std::byte> routing_id, std::span<const std::byte> payload) {
        HandleInnerMessage(routing_id, payload);
    });

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GmNode::OnRun()
{
    if (inner_network_ == nullptr || inner_service_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM node must be initialized before Run().");
    }

    const NodeErrorCode run_result = inner_network_->Run();
    if (run_result != NodeErrorCode::None)
    {
        return SetError(run_result, std::string(inner_network_->last_error_message()));
    }

    const NodeErrorCode inner_run_result = inner_service_->Run();
    if (inner_run_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(inner_service_->last_error_message());
        (void)inner_network_->Uninit();
        return SetError(inner_run_result, error_message);
    }

    const std::array<xs::core::LogContextField, 2> runtime_context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"innerNetworkEndpoint", std::string(inner_network_->bound_endpoint())},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "GM node entered inner-listening state.", runtime_context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GmNode::OnUninit()
{
    if (inner_service_ != nullptr)
    {
        const NodeErrorCode result = inner_service_->Uninit();
        const std::string error_message = std::string(inner_service_->last_error_message());
        inner_service_.reset();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }

    if (inner_network_ != nullptr)
    {
        const NodeErrorCode result = inner_network_->Uninit();
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }

    ClearError();
    return NodeErrorCode::None;
}

void GmNode::HandleInnerMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    if (inner_service_ == nullptr)
    {
        return;
    }

    xs::net::PacketHeader raw_header{};
    if (!TryReadRawPacketHeader(payload, &raw_header))
    {
        inner_service_->HandleInnerMessage(routing_id, payload);
        return;
    }

    if (raw_header.msg_id == xs::net::kInnerHeartbeatMsgId)
    {
        inner_service_->HandleInnerMessage(routing_id, payload);
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
        inner_service_->HandleInnerMessage(routing_id, payload);
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

        const NodeErrorCode send_result = inner_network_->Send(routing_id, response_buffer);
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
    ProcessRegistryRegistration registration{
        .process_type = request.process_type,
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

    const ProcessRegistryErrorCode register_result = inner_service_->RegisterProcess(std::move(registration));
    if (register_result != ProcessRegistryErrorCode::None)
    {
        const std::optional<std::int32_t> error_code = MapProcessRegistryErrorToInnerError(register_result);
        if (error_code.has_value())
        {
            send_error_response(*error_code, &request);
        }
        else
        {
            std::vector<xs::core::LogContextField> context = BuildRegisterContext(routing_id, packet.header.seq, &request);
            context.push_back(
                xs::core::LogContextField{"registryError", std::string(ProcessRegistryErrorMessage(register_result))});
            logger().Log(xs::core::LogLevel::Warn, "inner", "GM rejected register request without mapped error code.", context);
        }

        return;
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

} // namespace xs::node

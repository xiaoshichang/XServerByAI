#include "GmInnerService.h"

#include "BinarySerialization.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"

#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

constexpr std::int32_t kInnerNodeNotRegistered = 3003;
constexpr std::int32_t kInnerChannelInvalid = 3004;
constexpr std::int32_t kInnerRequestInvalid = 3005;

constexpr std::string_view kInnerNodeNotRegisteredName = "Inner.NodeNotRegistered";
constexpr std::string_view kInnerChannelInvalidName = "Inner.ChannelInvalid";
constexpr std::string_view kInnerRequestInvalidName = "Inner.RequestInvalid";

[[nodiscard]] std::string MakeRoutingKey(std::span<const std::byte> routing_id)
{
    return std::string(
        reinterpret_cast<const char*>(routing_id.data()),
        reinterpret_cast<const char*>(routing_id.data() + routing_id.size()));
}

[[nodiscard]] std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

[[nodiscard]] bool TryReadRawPacketHeader(
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

[[nodiscard]] bool IsHeartbeatRequestPacket(const xs::net::PacketHeader& header) noexcept
{
    return header.magic == xs::net::kPacketMagic &&
           header.version == xs::net::kPacketVersion &&
           header.msg_id == xs::net::kInnerHeartbeatMsgId;
}

[[nodiscard]] std::uint16_t HeartbeatResponseFlags(bool is_error) noexcept
{
    std::uint16_t flags = static_cast<std::uint16_t>(xs::net::PacketFlag::Response);
    if (is_error)
    {
        flags |= static_cast<std::uint16_t>(xs::net::PacketFlag::Error);
    }

    return flags;
}

} // namespace

GmInnerService::GmInnerService(
    xs::core::MainEventLoop& event_loop,
    xs::core::Logger& logger,
    InnerNetwork& inner_network,
    GmInnerServiceOptions options)
    : event_loop_(event_loop),
      logger_(logger),
      inner_network_(inner_network),
      options_(std::move(options))
{
}

GmInnerService::~GmInnerService() = default;

NodeErrorCode GmInnerService::Init()
{
    if (initialized_)
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM inner service is already initialized.");
    }

    if (options_.heartbeat_interval_ms == 0U)
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM heartbeat_interval_ms must be greater than zero.");
    }

    if (options_.heartbeat_timeout_ms == 0U)
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM heartbeat_timeout_ms must be greater than zero.");
    }

    if (options_.heartbeat_interval_ms >= options_.heartbeat_timeout_ms)
    {
        return SetError(
            NodeErrorCode::InvalidArgument,
            "GM heartbeat_interval_ms must be less than heartbeat_timeout_ms.");
    }

    if (options_.timeout_scan_interval <= std::chrono::milliseconds::zero())
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM timeout_scan_interval must be greater than zero.");
    }

    if (options_.invalidated_routing_retention < std::chrono::milliseconds::zero())
    {
        return SetError(
            NodeErrorCode::InvalidArgument,
            "GM invalidated_routing_retention must not be negative.");
    }

    inner_network_.SetListenerMessageHandler([this](std::vector<std::byte> routing_id, std::vector<std::byte> payload) {
        HandleInnerMessage(routing_id, payload);
    });

    initialized_ = true;
    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GmInnerService::Run()
{
    if (!initialized_)
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM inner service must be initialized before Run().");
    }

    if (running_)
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM inner service is already running.");
    }

    const xs::core::TimerCreateResult timer_result =
        event_loop_.timers().CreateRepeating(options_.timeout_scan_interval, [this]() { HandleTimeoutScan(); });
    if (!xs::core::IsTimerID(timer_result))
    {
        return SetError(
            NodeErrorCode::NodeRunFailed,
            "Failed to create GM timeout scan timer: " +
                std::string(xs::core::TimerErrorMessage(xs::core::TimerErrorFromCreateResult(timer_result))));
    }

    timeout_scan_timer_id_ = timer_result;
    running_ = true;
    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GmInnerService::Uninit()
{
    if (timeout_scan_timer_id_ > 0)
    {
        (void)event_loop_.timers().Cancel(timeout_scan_timer_id_);
        timeout_scan_timer_id_ = 0;
    }

    inner_network_.SetListenerMessageHandler({});
    running_ = false;
    initialized_ = false;
    ClearError();
    return NodeErrorCode::None;
}

InnerNetworkSessionManager& GmInnerService::inner_network_session_manager() noexcept
{
    return inner_network_session_manager_;
}

const InnerNetworkSessionManager& GmInnerService::inner_network_session_manager() const noexcept
{
    return inner_network_session_manager_;
}

InnerNetworkSessionManagerErrorCode GmInnerService::RegisterProcess(InnerNetworkSessionRegistration registration)
{
    if (registration.last_heartbeat_at_unix_ms == 0U)
    {
        registration.last_heartbeat_at_unix_ms = CurrentUnixTimeMilliseconds();
    }

    const InnerNetworkSessionManagerErrorCode result = inner_network_session_manager_.Register(registration);
    if (result == InnerNetworkSessionManagerErrorCode::None && !registration.routing_id.empty())
    {
        invalidated_routing_ids_.erase(MakeRoutingKey(registration.routing_id));
    }

    return result;
}

InnerNetworkSessionManagerErrorCode GmInnerService::UnregisterProcessByNodeId(std::string_view node_id)
{
    const InnerNetworkSession* entry = inner_network_session_manager_.FindByNodeId(node_id);
    RoutingID routing_id = entry != nullptr ? entry->routing_id : RoutingID{};
    const InnerNetworkSessionManagerErrorCode result = inner_network_session_manager_.UnregisterByNodeId(node_id);
    if (result == InnerNetworkSessionManagerErrorCode::None && !routing_id.empty())
    {
        RememberInvalidatedRoutingId(routing_id, CurrentUnixTimeMilliseconds());
    }

    return result;
}

void GmInnerService::InvalidateRoutingId(std::span<const std::byte> routing_id)
{
    RememberInvalidatedRoutingId(routing_id, CurrentUnixTimeMilliseconds());
}

bool GmInnerService::ContainsInvalidatedRoutingId(std::span<const std::byte> routing_id) const
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

std::string_view GmInnerService::last_error_message() const noexcept
{
    return last_error_message_;
}

NodeErrorCode GmInnerService::SetError(NodeErrorCode code, std::string message)
{
    if (message.empty())
    {
        last_error_message_ = std::string(NodeErrorMessage(code));
    }
    else
    {
        last_error_message_ = std::move(message);
    }

    return code;
}

void GmInnerService::ClearError() noexcept
{
    last_error_message_.clear();
}

std::uint64_t GmInnerService::CurrentUnixTimeMilliseconds() const noexcept
{
    const std::int64_t now_unix_ms = xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow());
    return now_unix_ms > 0 ? static_cast<std::uint64_t>(now_unix_ms) : 0U;
}

std::uint64_t GmInnerService::InvalidatedRoutingRetentionMs() const noexcept
{
    if (options_.invalidated_routing_retention > std::chrono::milliseconds::zero())
    {
        return static_cast<std::uint64_t>(options_.invalidated_routing_retention.count());
    }

    return static_cast<std::uint64_t>(options_.heartbeat_timeout_ms);
}

void GmInnerService::RememberInvalidatedRoutingId(
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

void GmInnerService::PruneExpiredInvalidatedRoutingIds(std::uint64_t now_unix_ms)
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

void GmInnerService::HandleInnerMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    if (routing_id.empty())
    {
        return;
    }

    HandleHeartbeatMessage(routing_id, payload);
}

void GmInnerService::HandleHeartbeatMessage(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();
    PruneExpiredInvalidatedRoutingIds(now_unix_ms);

    xs::net::PacketHeader raw_header{};
    if (!TryReadRawPacketHeader(payload, &raw_header))
    {
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
        };
        Log(xs::core::LogLevel::Warn, "GM inner service ignored a payload without a complete packet header.", context);
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

        const NodeErrorCode send_result = inner_network_.Send(routing_id, response_packet);
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"seq", ToString(static_cast<std::uint64_t>(raw_header.seq))},
        };
        if (send_result != NodeErrorCode::None)
        {
            Log(
                xs::core::LogLevel::Warn,
                "GM inner service failed to send heartbeat error response.",
                context,
                error_code,
                error_name);
            return;
        }

        Log(xs::core::LogLevel::Warn, log_message, context, error_code, error_name);
    };

    if (!xs::net::IsValidPacketFlags(raw_header.flags) ||
        raw_header.flags != 0U ||
        raw_header.seq == xs::net::kPacketSeqNone)
    {
        send_heartbeat_error(
            kInnerRequestInvalid,
            kInnerRequestInvalidName,
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
            kInnerRequestInvalidName,
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
            kInnerRequestInvalidName,
            false,
            "GM inner service rejected a malformed heartbeat payload.");
        return;
    }

    const InnerNetworkSession* entry = inner_network_session_manager_.FindByRoutingId(routing_id);
    if (entry == nullptr)
    {
        const bool invalidated = ContainsInvalidatedRoutingId(routing_id);
        send_heartbeat_error(
            invalidated ? kInnerChannelInvalid : kInnerNodeNotRegistered,
            invalidated ? kInnerChannelInvalidName : kInnerNodeNotRegisteredName,
            true,
            invalidated
                ? "GM inner service rejected heartbeat on an invalidated inner channel."
                : "GM inner service rejected heartbeat from an unknown inner channel.");
        return;
    }

    const InnerNetworkSessionManagerErrorCode update_result =
        inner_network_session_manager_.UpdateHeartbeatByRoutingId(routing_id, now_unix_ms, request.load);
    if (update_result != InnerNetworkSessionManagerErrorCode::None)
    {
        send_heartbeat_error(
            kInnerNodeNotRegistered,
            kInnerNodeNotRegisteredName,
            true,
            "GM inner service lost the active inner network session while handling heartbeat.");
        return;
    }

    const xs::net::HeartbeatSuccessResponse response{
        .heartbeat_interval_ms = options_.heartbeat_interval_ms,
        .heartbeat_timeout_ms = options_.heartbeat_timeout_ms,
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

    const NodeErrorCode send_result = inner_network_.Send(routing_id, response_packet);
    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"nodeId", std::string(entry->node_id)},
        xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
        xs::core::LogContextField{"seq", ToString(static_cast<std::uint64_t>(packet.header.seq))},
        xs::core::LogContextField{"loadScore", ToString(static_cast<std::uint64_t>(request.load.load_score))},
    };
    if (send_result != NodeErrorCode::None)
    {
        Log(xs::core::LogLevel::Warn, "GM inner service failed to send heartbeat success response.", context);
        return;
    }

    Log(xs::core::LogLevel::Info, "GM inner service refreshed heartbeat state.", context);
}

void GmInnerService::HandleTimeoutScan()
{
    const std::uint64_t now_unix_ms = CurrentUnixTimeMilliseconds();
    PruneExpiredInvalidatedRoutingIds(now_unix_ms);

    const std::vector<InnerNetworkSession> snapshot = inner_network_session_manager_.Snapshot();
    for (const InnerNetworkSession& entry : snapshot)
    {
        if (entry.last_heartbeat_at_unix_ms > now_unix_ms)
        {
            continue;
        }

        const std::uint64_t elapsed_ms = now_unix_ms - entry.last_heartbeat_at_unix_ms;
        if (elapsed_ms < static_cast<std::uint64_t>(options_.heartbeat_timeout_ms))
        {
            continue;
        }

        const InnerNetworkSessionManagerErrorCode unregister_result =
            inner_network_session_manager_.UnregisterByNodeId(entry.node_id);
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
        Log(
            xs::core::LogLevel::Warn,
            "GM inner service evicted a timed-out inner network session.",
            context,
            kInnerChannelInvalid,
            kInnerChannelInvalidName);
    }
}

void GmInnerService::Log(
    xs::core::LogLevel level,
    std::string_view message,
    std::span<const xs::core::LogContextField> context,
    std::optional<std::int32_t> error_code,
    std::string_view error_name) const
{
    logger_.Log(level, "inner", message, context, error_code, error_name);
}

} // namespace xs::node

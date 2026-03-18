#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace xs::net
{

inline constexpr std::uint32_t kControlProcessHeartbeatMsgId = 1100u;
inline constexpr std::size_t kControlLoadSnapshotSize = sizeof(std::uint32_t) * 5u;
inline constexpr std::size_t kProcessHeartbeatRequestSize = sizeof(std::uint64_t) + sizeof(std::uint64_t) + sizeof(std::uint32_t) + kControlLoadSnapshotSize;
inline constexpr std::size_t kProcessHeartbeatSuccessResponseSize = sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
inline constexpr std::size_t kProcessHeartbeatErrorResponseSize = sizeof(std::int32_t) + sizeof(std::uint32_t) + sizeof(std::uint8_t);

enum class ProcessHeartbeatCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidBoolValue = 3,
    InvalidArgument = 4,
    InvalidRegistrationId = 5,
    InvalidStatusFlags = 6,
    InvalidHeartbeatTiming = 7,
    TrailingBytes = 8,
};

[[nodiscard]] std::string_view ProcessHeartbeatCodecErrorMessage(ProcessHeartbeatCodecErrorCode error_code) noexcept;

struct LoadSnapshot
{
    std::uint32_t connection_count{0};
    std::uint32_t session_count{0};
    std::uint32_t entity_count{0};
    std::uint32_t space_count{0};
    std::uint32_t load_score{0};
};

struct ProcessHeartbeatRequest
{
    std::uint64_t registration_id{0};
    std::uint64_t sent_at_unix_ms{0};
    std::uint32_t status_flags{0};
    LoadSnapshot load{};
};

struct ProcessHeartbeatSuccessResponse
{
    std::uint64_t registration_id{0};
    std::uint32_t heartbeat_interval_ms{0};
    std::uint32_t heartbeat_timeout_ms{0};
    std::uint64_t server_now_unix_ms{0};
};

struct ProcessHeartbeatErrorResponse
{
    std::int32_t error_code{0};
    std::uint32_t retry_after_ms{0};
    bool require_full_register{false};
};

[[nodiscard]] ProcessHeartbeatCodecErrorCode EncodeProcessHeartbeatRequest(
    const ProcessHeartbeatRequest& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] ProcessHeartbeatCodecErrorCode DecodeProcessHeartbeatRequest(
    std::span<const std::byte> buffer,
    ProcessHeartbeatRequest* message) noexcept;

[[nodiscard]] ProcessHeartbeatCodecErrorCode EncodeProcessHeartbeatSuccessResponse(
    const ProcessHeartbeatSuccessResponse& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] ProcessHeartbeatCodecErrorCode DecodeProcessHeartbeatSuccessResponse(
    std::span<const std::byte> buffer,
    ProcessHeartbeatSuccessResponse* message) noexcept;

[[nodiscard]] ProcessHeartbeatCodecErrorCode EncodeProcessHeartbeatErrorResponse(
    const ProcessHeartbeatErrorResponse& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] ProcessHeartbeatCodecErrorCode DecodeProcessHeartbeatErrorResponse(
    std::span<const std::byte> buffer,
    ProcessHeartbeatErrorResponse* message) noexcept;

} // namespace xs::net
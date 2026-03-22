#pragma once

#include "InnerMessageTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace xs::net
{

inline constexpr std::uint32_t kInnerHeartbeatMsgId = 1100u;
inline constexpr std::size_t kHeartbeatRequestSize = sizeof(std::uint64_t) + sizeof(std::uint32_t) + kInnerLoadSnapshotSize;
inline constexpr std::size_t kHeartbeatSuccessResponseSize = sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
inline constexpr std::size_t kHeartbeatErrorResponseSize = sizeof(std::int32_t) + sizeof(std::uint32_t) + sizeof(std::uint8_t);

enum class HeartbeatCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidBoolValue = 3,
    InvalidArgument = 4,
    InvalidStatusFlags = 5,
    InvalidHeartbeatTiming = 6,
    TrailingBytes = 7,
};

[[nodiscard]] std::string_view HeartbeatCodecErrorMessage(HeartbeatCodecErrorCode error_code) noexcept;

struct HeartbeatRequest
{
    std::uint64_t sent_at_unix_ms{0};
    std::uint32_t status_flags{0};
    LoadSnapshot load{};
};

struct HeartbeatSuccessResponse
{
    std::uint32_t heartbeat_interval_ms{0};
    std::uint32_t heartbeat_timeout_ms{0};
    std::uint64_t server_now_unix_ms{0};
};

struct HeartbeatErrorResponse
{
    std::int32_t error_code{0};
    std::uint32_t retry_after_ms{0};
    bool require_full_register{false};
};

[[nodiscard]] HeartbeatCodecErrorCode EncodeHeartbeatRequest(
    const HeartbeatRequest& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] HeartbeatCodecErrorCode DecodeHeartbeatRequest(
    std::span<const std::byte> buffer,
    HeartbeatRequest* message) noexcept;

[[nodiscard]] HeartbeatCodecErrorCode EncodeHeartbeatSuccessResponse(
    const HeartbeatSuccessResponse& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] HeartbeatCodecErrorCode DecodeHeartbeatSuccessResponse(
    std::span<const std::byte> buffer,
    HeartbeatSuccessResponse* message) noexcept;

[[nodiscard]] HeartbeatCodecErrorCode EncodeHeartbeatErrorResponse(
    const HeartbeatErrorResponse& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] HeartbeatCodecErrorCode DecodeHeartbeatErrorResponse(
    std::span<const std::byte> buffer,
    HeartbeatErrorResponse* message) noexcept;

} // namespace xs::net


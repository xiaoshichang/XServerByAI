#pragma once

#include "MessageIds.h"
#include "InnerMessageTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace xs::net
{

inline constexpr std::size_t kHeartbeatRequestSize = sizeof(std::uint64_t) + sizeof(std::uint32_t) + kInnerLoadSnapshotSize;
inline constexpr std::size_t kHeartbeatSuccessResponseSize = sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);

enum class HeartbeatCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidArgument = 3,
    InvalidStatusFlags = 4,
    InvalidHeartbeatTiming = 5,
    TrailingBytes = 6,
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

} // namespace xs::net


#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace xs::net
{

inline constexpr std::uint32_t kInnerClusterReadyNotifyMsgId = 1201u;
inline constexpr std::size_t kClusterReadyNotifySize =
    sizeof(std::uint64_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);

enum class InnerClusterCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    InvalidBoolValue = 2,
    InvalidArgument = 3,
    InvalidReadyStatusFlags = 4,
    TrailingBytes = 5,
};

[[nodiscard]] std::string_view InnerClusterCodecErrorMessage(InnerClusterCodecErrorCode error_code) noexcept;

struct ClusterReadyNotify
{
    std::uint64_t ready_epoch{0};
    bool cluster_ready{false};
    std::uint32_t status_flags{0};
    std::uint64_t server_now_unix_ms{0};
};

[[nodiscard]] InnerClusterCodecErrorCode EncodeClusterReadyNotify(
    const ClusterReadyNotify& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] InnerClusterCodecErrorCode DecodeClusterReadyNotify(
    std::span<const std::byte> buffer,
    ClusterReadyNotify* message) noexcept;

} // namespace xs::net

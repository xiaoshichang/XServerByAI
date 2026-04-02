#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xs::net
{

inline constexpr std::uint32_t kRelayForwardToGameMsgId = 2000u;
inline constexpr std::uint32_t kRelayPushToClientMsgId = 2001u;
inline constexpr std::uint32_t kRelayForwardStubCallMsgId = 2002u;

enum class RelayCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidArgument = 3,
    InvalidSourceGameNodeId = 4,
    InvalidTargetGameNodeId = 5,
    InvalidTargetStubType = 6,
    InvalidMessageId = 7,
    InvalidRelayFlags = 8,
    TrailingBytes = 9,
};

[[nodiscard]] std::string_view RelayCodecErrorMessage(RelayCodecErrorCode error_code) noexcept;

struct RelayForwardStubCall
{
    std::string source_game_node_id{};
    std::string target_game_node_id{};
    std::string target_stub_type{};
    std::uint32_t stub_call_msg_id{0u};
    std::uint32_t relay_flags{0u};
    std::vector<std::byte> payload{};
};

[[nodiscard]] RelayCodecErrorCode GetRelayForwardStubCallWireSize(
    const RelayForwardStubCall& message,
    std::size_t* wire_size) noexcept;
[[nodiscard]] RelayCodecErrorCode EncodeRelayForwardStubCall(
    const RelayForwardStubCall& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] RelayCodecErrorCode DecodeRelayForwardStubCall(
    std::span<const std::byte> buffer,
    RelayForwardStubCall* message) noexcept;

} // namespace xs::net

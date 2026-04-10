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
inline constexpr std::uint32_t kRelayForwardMailboxCallMsgId = 2002u;
inline constexpr std::uint32_t kRelayForwardProxyCallMsgId = 2005u;

enum class RelayCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidArgument = 3,
    InvalidSourceGameNodeId = 4,
    InvalidTargetGameNodeId = 5,
    InvalidTargetMailboxName = 6,
    InvalidRouteGateNodeId = 7,
    InvalidTargetEntityId = 8,
    InvalidMessageId = 9,
    InvalidRelayFlags = 10,
    TrailingBytes = 11,
};

[[nodiscard]] std::string_view RelayCodecErrorMessage(RelayCodecErrorCode error_code) noexcept;

struct RelayForwardMailboxCall
{
    std::string source_game_node_id{};
    std::string target_game_node_id{};
    std::string target_entity_id{};
    std::string target_mailbox_name{};
    std::uint32_t mailbox_call_msg_id{0u};
    std::uint32_t relay_flags{0u};
    std::vector<std::byte> payload{};
};

struct RelayForwardProxyCall
{
    std::string source_game_node_id{};
    std::string route_gate_node_id{};
    std::string target_entity_id{};
    std::uint32_t proxy_call_msg_id{0u};
    std::uint32_t relay_flags{0u};
    std::vector<std::byte> payload{};
};

struct RelayPushToClient
{
    std::string source_game_node_id{};
    std::string route_gate_node_id{};
    std::string target_entity_id{};
    std::uint32_t client_msg_id{0u};
    std::uint32_t relay_flags{0u};
    std::vector<std::byte> payload{};
};

[[nodiscard]] RelayCodecErrorCode GetRelayForwardMailboxCallWireSize(
    const RelayForwardMailboxCall& message,
    std::size_t* wire_size) noexcept;
[[nodiscard]] RelayCodecErrorCode EncodeRelayForwardMailboxCall(
    const RelayForwardMailboxCall& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] RelayCodecErrorCode DecodeRelayForwardMailboxCall(
    std::span<const std::byte> buffer,
    RelayForwardMailboxCall* message) noexcept;

[[nodiscard]] RelayCodecErrorCode GetRelayForwardProxyCallWireSize(
    const RelayForwardProxyCall& message,
    std::size_t* wire_size) noexcept;
[[nodiscard]] RelayCodecErrorCode EncodeRelayForwardProxyCall(
    const RelayForwardProxyCall& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] RelayCodecErrorCode DecodeRelayForwardProxyCall(
    std::span<const std::byte> buffer,
    RelayForwardProxyCall* message) noexcept;

[[nodiscard]] RelayCodecErrorCode GetRelayPushToClientWireSize(
    const RelayPushToClient& message,
    std::size_t* wire_size) noexcept;
[[nodiscard]] RelayCodecErrorCode EncodeRelayPushToClient(
    const RelayPushToClient& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] RelayCodecErrorCode DecodeRelayPushToClient(
    std::span<const std::byte> buffer,
    RelayPushToClient* message) noexcept;

} // namespace xs::net

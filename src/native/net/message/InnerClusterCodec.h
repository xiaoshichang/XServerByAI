#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xs::net
{

inline constexpr std::uint32_t kInnerClusterReadyNotifyMsgId = 1201u;
inline constexpr std::uint32_t kInnerServerStubOwnershipSyncMsgId = 1202u;
inline constexpr std::uint32_t kInnerGameServiceReadyReportMsgId = 1203u;
inline constexpr std::uint32_t kInnerClusterNodesOnlineNotifyMsgId = 1204u;
inline constexpr std::uint32_t kInnerGameGateMeshReadyReportMsgId = 1205u;
inline constexpr std::size_t kClusterNodesOnlineNotifySize =
    sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
inline constexpr std::size_t kClusterReadyNotifySize =
    sizeof(std::uint64_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
inline constexpr std::size_t kGameGateMeshReadyReportSize =
    sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);

enum class InnerClusterCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    InvalidBoolValue = 2,
    InvalidArgument = 3,
    InvalidReadyStatusFlags = 4,
    TrailingBytes = 5,
    InvalidNodesOnlineStatusFlags = 6,
    LengthOverflow = 7,
    InvalidMeshReadyStatusFlags = 8,
    InvalidOwnershipStatusFlags = 9,
    InvalidOwnershipEntryFlags = 10,
    InvalidServiceReadyStatusFlags = 11,
    InvalidServiceReadyEntryFlags = 12,
};

[[nodiscard]] std::string_view InnerClusterCodecErrorMessage(InnerClusterCodecErrorCode error_code) noexcept;

struct ClusterNodesOnlineNotify
{
    bool all_nodes_online{false};
    std::uint32_t status_flags{0};
    std::uint64_t server_now_unix_ms{0};
};

[[nodiscard]] InnerClusterCodecErrorCode EncodeClusterNodesOnlineNotify(
    const ClusterNodesOnlineNotify& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] InnerClusterCodecErrorCode DecodeClusterNodesOnlineNotify(
    std::span<const std::byte> buffer,
    ClusterNodesOnlineNotify* message) noexcept;

struct GameGateMeshReadyReport
{
    bool mesh_ready{false};
    std::uint32_t status_flags{0};
    std::uint64_t reported_at_unix_ms{0};
};

[[nodiscard]] InnerClusterCodecErrorCode EncodeGameGateMeshReadyReport(
    const GameGateMeshReadyReport& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] InnerClusterCodecErrorCode DecodeGameGateMeshReadyReport(
    std::span<const std::byte> buffer,
    GameGateMeshReadyReport* message) noexcept;

struct ServerStubOwnershipEntry
{
    std::string entity_type{};
    std::string entity_id{};
    std::string owner_game_node_id{};
    std::uint32_t entry_flags{0};
};

struct ServerStubOwnershipSync
{
    std::uint64_t assignment_epoch{0};
    std::uint32_t status_flags{0};
    std::vector<ServerStubOwnershipEntry> assignments{};
    std::uint64_t server_now_unix_ms{0};
};

[[nodiscard]] InnerClusterCodecErrorCode GetServerStubOwnershipSyncWireSize(
    const ServerStubOwnershipSync& message,
    std::size_t* wire_size) noexcept;
[[nodiscard]] InnerClusterCodecErrorCode EncodeServerStubOwnershipSync(
    const ServerStubOwnershipSync& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] InnerClusterCodecErrorCode DecodeServerStubOwnershipSync(
    std::span<const std::byte> buffer,
    ServerStubOwnershipSync* message) noexcept;

struct ServerStubReadyEntry
{
    std::string entity_type{};
    std::string entity_id{};
    bool ready{false};
    std::uint32_t entry_flags{0};
};

struct GameServiceReadyReport
{
    std::uint64_t assignment_epoch{0};
    bool local_ready{false};
    std::uint32_t status_flags{0};
    std::vector<ServerStubReadyEntry> entries{};
    std::uint64_t reported_at_unix_ms{0};
};

[[nodiscard]] InnerClusterCodecErrorCode GetGameServiceReadyReportWireSize(
    const GameServiceReadyReport& message,
    std::size_t* wire_size) noexcept;
[[nodiscard]] InnerClusterCodecErrorCode EncodeGameServiceReadyReport(
    const GameServiceReadyReport& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] InnerClusterCodecErrorCode DecodeGameServiceReadyReport(
    std::span<const std::byte> buffer,
    GameServiceReadyReport* message) noexcept;

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
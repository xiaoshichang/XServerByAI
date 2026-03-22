#pragma once

#include "message/InnerMessageTypes.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xs::node
{

enum class ProcessRegistryErrorCode : std::uint8_t
{
    None = 0,
    InvalidArgument,
    InvalidProcessType,
    InvalidNodeId,
    InvalidInnerNetworkEndpointHost,
    InvalidInnerNetworkEndpointPort,
    NodeIdConflict,
    RoutingIdConflict,
    NodeNotFound,
    RoutingIdNotFound,
};

[[nodiscard]] std::string_view ProcessRegistryErrorMessage(ProcessRegistryErrorCode code) noexcept;

using RoutingID = std::vector<std::byte>;

struct ProcessRegistryRegistration
{
    std::uint16_t process_type{0};
    std::string node_id{};
    std::uint32_t pid{0};
    std::uint64_t started_at_unix_ms{0};
    xs::net::Endpoint inner_network_endpoint{};
    std::string build_version{};
    std::vector<std::string> capability_tags{};
    xs::net::LoadSnapshot load{};
    RoutingID routing_id{};
    std::uint64_t last_heartbeat_at_unix_ms{0};
    bool inner_network_ready{false};
};

struct ProcessRegistryEntry
{
    xs::net::InnerProcessType process_type{xs::net::InnerProcessType::Gate};
    std::string node_id{};
    std::uint32_t pid{0};
    std::uint64_t started_at_unix_ms{0};
    xs::net::Endpoint inner_network_endpoint{};
    std::string build_version{};
    std::vector<std::string> capability_tags{};
    xs::net::LoadSnapshot load{};
    RoutingID routing_id{};
    std::uint64_t last_heartbeat_at_unix_ms{0};
    bool inner_network_ready{false};
};

class ProcessRegistry final
{
  public:
    [[nodiscard]] ProcessRegistryErrorCode Register(const ProcessRegistryRegistration& registration);
    [[nodiscard]] ProcessRegistryErrorCode UnregisterByNodeId(std::string_view node_id);
    [[nodiscard]] ProcessRegistryErrorCode UnregisterByRoutingId(std::span<const std::byte> routing_id);

    [[nodiscard]] ProcessRegistryErrorCode UpdateHeartbeatByNodeId(
        std::string_view node_id,
        std::uint64_t last_heartbeat_at_unix_ms,
        const xs::net::LoadSnapshot& load);
    [[nodiscard]] ProcessRegistryErrorCode UpdateHeartbeatByRoutingId(
        std::span<const std::byte> routing_id,
        std::uint64_t last_heartbeat_at_unix_ms,
        const xs::net::LoadSnapshot& load);

    [[nodiscard]] ProcessRegistryErrorCode UpdateInnerNetworkReadyByNodeId(
        std::string_view node_id,
        bool inner_network_ready);
    [[nodiscard]] ProcessRegistryErrorCode UpdateInnerNetworkReadyByRoutingId(
        std::span<const std::byte> routing_id,
        bool inner_network_ready);

    [[nodiscard]] const ProcessRegistryEntry* FindByNodeId(std::string_view node_id) const;
    [[nodiscard]] const ProcessRegistryEntry* FindByRoutingId(std::span<const std::byte> routing_id) const;

    [[nodiscard]] bool ContainsNodeId(std::string_view node_id) const;
    [[nodiscard]] bool ContainsRoutingId(std::span<const std::byte> routing_id) const;
    [[nodiscard]] std::vector<ProcessRegistryEntry> Snapshot() const;
    [[nodiscard]] std::size_t size() const noexcept;

    void Clear() noexcept;

  private:
    [[nodiscard]] ProcessRegistryEntry* FindMutableByNodeId(std::string_view node_id);
    [[nodiscard]] ProcessRegistryEntry* FindMutableByRoutingId(std::span<const std::byte> routing_id);

    std::map<std::string, ProcessRegistryEntry, std::less<>> entries_by_node_id_{};
    std::unordered_map<std::string, std::string> node_id_by_routing_key_{};
};

} // namespace xs::node

#pragma once

#include "Logging.h"
#include "MainEventLoop.h"
#include "ZmqActiveConnector.h"
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

enum class InnerNetworkSessionManagerErrorCode : std::uint8_t
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

[[nodiscard]] std::string_view InnerNetworkSessionManagerErrorMessage(
    InnerNetworkSessionManagerErrorCode code) noexcept;

using RoutingID = std::vector<std::byte>;

struct InnerNetworkSessionRegistration
{
    xs::core::ProcessType process_type{xs::core::ProcessType::Gm};
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

struct InnerNetworkSession
{
    xs::core::ProcessType process_type{xs::core::ProcessType::Gm};
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
    xs::ipc::ZmqConnectionState connection_state{xs::ipc::ZmqConnectionState::Stopped};
    std::uint32_t next_seq{1U};
    std::uint32_t register_seq{0U};
    std::uint32_t heartbeat_seq{0U};
    std::uint32_t heartbeat_interval_ms{0U};
    std::uint32_t heartbeat_timeout_ms{0U};
    std::uint64_t last_server_now_unix_ms{0U};
    xs::core::TimerID heartbeat_timer_id{0};
    std::string last_protocol_error{};
    bool registered{false};
    bool register_in_flight{false};
};

class InnerNetworkSessionManager final
{
  public:
    [[nodiscard]] InnerNetworkSessionManagerErrorCode Register(const InnerNetworkSessionRegistration& registration);
    [[nodiscard]] InnerNetworkSessionManagerErrorCode UnregisterByNodeId(std::string_view node_id);
    [[nodiscard]] InnerNetworkSessionManagerErrorCode UnregisterByRoutingId(std::span<const std::byte> routing_id);

    [[nodiscard]] InnerNetworkSessionManagerErrorCode UpdateHeartbeatByNodeId(
        std::string_view node_id,
        std::uint64_t last_heartbeat_at_unix_ms,
        const xs::net::LoadSnapshot& load);
    [[nodiscard]] InnerNetworkSessionManagerErrorCode UpdateHeartbeatByRoutingId(
        std::span<const std::byte> routing_id,
        std::uint64_t last_heartbeat_at_unix_ms,
        const xs::net::LoadSnapshot& load);

    [[nodiscard]] InnerNetworkSessionManagerErrorCode UpdateInnerNetworkReadyByNodeId(
        std::string_view node_id,
        bool inner_network_ready);
    [[nodiscard]] InnerNetworkSessionManagerErrorCode UpdateInnerNetworkReadyByRoutingId(
        std::span<const std::byte> routing_id,
        bool inner_network_ready);

    [[nodiscard]] const InnerNetworkSession* FindByNodeId(std::string_view node_id) const;
    [[nodiscard]] const InnerNetworkSession* FindByRoutingId(std::span<const std::byte> routing_id) const;
    [[nodiscard]] InnerNetworkSession* FindMutableByNodeId(std::string_view node_id);
    [[nodiscard]] InnerNetworkSession* FindMutableByRoutingId(std::span<const std::byte> routing_id);

    [[nodiscard]] bool ContainsNodeId(std::string_view node_id) const;
    [[nodiscard]] bool ContainsRoutingId(std::span<const std::byte> routing_id) const;
    [[nodiscard]] std::vector<InnerNetworkSession> Snapshot() const;
    [[nodiscard]] std::size_t size() const noexcept;

    void Clear() noexcept;

  private:
    std::map<std::string, InnerNetworkSession, std::less<>> entries_by_node_id_{};
    std::unordered_map<std::string, std::string> node_id_by_routing_key_{};
};

} // namespace xs::node

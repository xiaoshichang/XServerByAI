#pragma once

#include "KcpPeer.h"
#include "message/InnerMessageTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace xs::node
{

enum class ClientSessionState : std::uint16_t
{
    Created = 1,
    Authenticating = 2,
    Active = 3,
    Closing = 4,
    Closed = 5,
};

[[nodiscard]] std::string_view ClientSessionStateName(ClientSessionState state) noexcept;

enum class ClientRouteState : std::uint16_t
{
    Unassigned = 1,
    Selecting = 2,
    Bound = 3,
    RouteLost = 4,
    Released = 5,
};

[[nodiscard]] std::string_view ClientRouteStateName(ClientRouteState state) noexcept;

enum class ClientSessionErrorCode : std::uint8_t
{
    None = 0,
    InvalidArgument,
    InvalidState,
    InvalidSessionStateTransition,
    InvalidRouteStateTransition,
    InvalidRouteTarget,
};

[[nodiscard]] std::string_view ClientSessionErrorMessage(ClientSessionErrorCode error_code) noexcept;

struct ClientRouteTarget
{
    std::string game_node_id{};
    xs::net::Endpoint inner_network_endpoint{};
    std::uint64_t route_epoch{0U};
};

struct ClientSessionOptions
{
    std::string gate_node_id{};
    std::uint64_t session_id{0U};
    std::uint32_t conversation{0U};
    xs::net::Endpoint remote_endpoint{};
    xs::core::KcpConfig kcp{};
    std::uint64_t connected_at_unix_ms{0U};
};

struct ClientSessionSnapshot
{
    std::string gate_node_id{};
    std::uint64_t session_id{0U};
    std::uint32_t conversation{0U};
    xs::net::Endpoint remote_endpoint{};
    ClientSessionState session_state{ClientSessionState::Created};
    std::uint64_t player_id{0U};
    ClientRouteState route_state{ClientRouteState::Unassigned};
    std::optional<ClientRouteTarget> route_target{};
    std::uint64_t connected_at_unix_ms{0U};
    std::uint64_t authenticated_at_unix_ms{0U};
    std::uint64_t last_active_unix_ms{0U};
    std::uint64_t closed_at_unix_ms{0U};
    std::int32_t close_reason_code{0};
};

class ClientSession final
{
  public:
    explicit ClientSession(ClientSessionOptions options);
    ~ClientSession() = default;

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;
    ClientSession(ClientSession&&) = delete;
    ClientSession& operator=(ClientSession&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::string_view gate_node_id() const noexcept;
    [[nodiscard]] std::uint64_t session_id() const noexcept;
    [[nodiscard]] std::uint32_t conversation() const noexcept;
    [[nodiscard]] const xs::net::Endpoint& remote_endpoint() const noexcept;
    [[nodiscard]] ClientSessionState session_state() const noexcept;
    [[nodiscard]] ClientRouteState route_state() const noexcept;
    [[nodiscard]] std::uint64_t player_id() const noexcept;
    [[nodiscard]] std::uint64_t connected_at_unix_ms() const noexcept;
    [[nodiscard]] std::uint64_t authenticated_at_unix_ms() const noexcept;
    [[nodiscard]] std::uint64_t last_active_unix_ms() const noexcept;
    [[nodiscard]] std::uint64_t closed_at_unix_ms() const noexcept;
    [[nodiscard]] std::int32_t close_reason_code() const noexcept;
    [[nodiscard]] const std::optional<ClientRouteTarget>& route_target() const noexcept;
    [[nodiscard]] xs::net::KcpPeer& kcp() noexcept;
    [[nodiscard]] const xs::net::KcpPeer& kcp() const noexcept;
    [[nodiscard]] ClientSessionSnapshot snapshot() const;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

    [[nodiscard]] ClientSessionErrorCode BeginAuthentication() noexcept;
    [[nodiscard]] ClientSessionErrorCode Activate(
        std::uint64_t authenticated_at_unix_ms = 0U,
        std::uint64_t player_id = 0U) noexcept;
    [[nodiscard]] ClientSessionErrorCode BeginClosing() noexcept;
    [[nodiscard]] ClientSessionErrorCode Close(
        std::int32_t close_reason_code,
        std::uint64_t closed_at_unix_ms) noexcept;
    [[nodiscard]] ClientSessionErrorCode SetRouteSelecting() noexcept;
    [[nodiscard]] ClientSessionErrorCode BindRoute(ClientRouteTarget route_target);
    [[nodiscard]] ClientSessionErrorCode MarkRouteLost() noexcept;
    [[nodiscard]] ClientSessionErrorCode ReleaseRoute() noexcept;
    void Touch(std::uint64_t last_active_unix_ms) noexcept;

  private:
    [[nodiscard]] ClientSessionErrorCode SetError(ClientSessionErrorCode code, std::string message = {});
    void ClearError() noexcept;
    [[nodiscard]] bool CanTransitionTo(ClientSessionState next_state) const noexcept;
    [[nodiscard]] bool CanTransitionRouteTo(ClientRouteState next_state) const noexcept;

    std::string gate_node_id_{};
    std::uint64_t session_id_{0U};
    xs::net::Endpoint remote_endpoint_{};
    xs::net::KcpPeer kcp_{};
    ClientSessionState session_state_{ClientSessionState::Created};
    ClientRouteState route_state_{ClientRouteState::Unassigned};
    std::uint64_t player_id_{0U};
    std::optional<ClientRouteTarget> route_target_{};
    std::uint64_t connected_at_unix_ms_{0U};
    std::uint64_t authenticated_at_unix_ms_{0U};
    std::uint64_t last_active_unix_ms_{0U};
    std::uint64_t closed_at_unix_ms_{0U};
    std::int32_t close_reason_code_{0};
    bool valid_{false};
    std::string last_error_message_{};
};

} // namespace xs::node

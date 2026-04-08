#include "ClientSession.h"

#include <utility>

namespace xs::node
{

std::string_view ClientSessionStateName(ClientSessionState state) noexcept
{
    switch (state)
    {
    case ClientSessionState::Created:
        return "Created";
    case ClientSessionState::Authenticating:
        return "Authenticating";
    case ClientSessionState::Active:
        return "Active";
    case ClientSessionState::Closing:
        return "Closing";
    case ClientSessionState::Closed:
        return "Closed";
    }

    return "Unknown";
}

std::string_view ClientRouteStateName(ClientRouteState state) noexcept
{
    switch (state)
    {
    case ClientRouteState::Unassigned:
        return "Unassigned";
    case ClientRouteState::Selecting:
        return "Selecting";
    case ClientRouteState::Bound:
        return "Bound";
    case ClientRouteState::RouteLost:
        return "RouteLost";
    case ClientRouteState::Released:
        return "Released";
    }

    return "Unknown";
}

std::string_view ClientSessionErrorMessage(ClientSessionErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case ClientSessionErrorCode::None:
        return "Success.";
    case ClientSessionErrorCode::InvalidArgument:
        return "Client session argument is invalid.";
    case ClientSessionErrorCode::InvalidState:
        return "Client session is not valid.";
    case ClientSessionErrorCode::InvalidSessionStateTransition:
        return "Client session state transition is invalid.";
    case ClientSessionErrorCode::InvalidRouteStateTransition:
        return "Client session route-state transition is invalid.";
    case ClientSessionErrorCode::InvalidRouteTarget:
        return "Client session route target is invalid.";
    }

    return "Unknown client session error.";
}

ClientSession::ClientSession(ClientSessionOptions options)
    : gate_node_id_(std::move(options.gate_node_id))
    , session_id_(options.session_id)
    , remote_endpoint_(std::move(options.remote_endpoint))
    , kcp_(xs::net::KcpPeerOptions{
          .conversation = options.conversation,
          .config = options.kcp,
      })
    , connected_at_unix_ms_(options.connected_at_unix_ms)
{
    if (gate_node_id_.empty())
    {
        (void)SetError(ClientSessionErrorCode::InvalidArgument, "Client session gate_node_id must not be empty.");
        return;
    }

    if (session_id_ == 0U)
    {
        (void)SetError(ClientSessionErrorCode::InvalidArgument, "Client session session_id must be greater than zero.");
        return;
    }

    if (remote_endpoint_.host.empty())
    {
        (void)SetError(ClientSessionErrorCode::InvalidArgument, "Client session remote endpoint host must not be empty.");
        return;
    }

    if (remote_endpoint_.port == 0U)
    {
        (void)SetError(ClientSessionErrorCode::InvalidArgument, "Client session remote endpoint port must be greater than zero.");
        return;
    }

    if (!kcp_.valid())
    {
        (void)SetError(
            ClientSessionErrorCode::InvalidState,
            "Client session failed to initialize KCP peer: " + std::string(kcp_.last_error_message()));
        return;
    }

    valid_ = true;
    ClearError();
}

bool ClientSession::valid() const noexcept
{
    return valid_;
}

std::string_view ClientSession::gate_node_id() const noexcept
{
    return gate_node_id_;
}

std::uint64_t ClientSession::session_id() const noexcept
{
    return session_id_;
}

std::uint32_t ClientSession::conversation() const noexcept
{
    return kcp_.conversation();
}

const xs::net::Endpoint& ClientSession::remote_endpoint() const noexcept
{
    return remote_endpoint_;
}

ClientSessionState ClientSession::session_state() const noexcept
{
    return session_state_;
}

ClientRouteState ClientSession::route_state() const noexcept
{
    return route_state_;
}

std::uint64_t ClientSession::connected_at_unix_ms() const noexcept
{
    return connected_at_unix_ms_;
}

std::uint64_t ClientSession::authenticated_at_unix_ms() const noexcept
{
    return authenticated_at_unix_ms_;
}

std::uint64_t ClientSession::last_active_unix_ms() const noexcept
{
    return last_active_unix_ms_;
}

std::uint64_t ClientSession::closed_at_unix_ms() const noexcept
{
    return closed_at_unix_ms_;
}

std::int32_t ClientSession::close_reason_code() const noexcept
{
    return close_reason_code_;
}

const std::optional<ClientRouteTarget>& ClientSession::route_target() const noexcept
{
    return route_target_;
}

xs::net::KcpPeer& ClientSession::kcp() noexcept
{
    return kcp_;
}

const xs::net::KcpPeer& ClientSession::kcp() const noexcept
{
    return kcp_;
}

ClientSessionSnapshot ClientSession::snapshot() const
{
    return ClientSessionSnapshot{
        .gate_node_id = gate_node_id_,
        .session_id = session_id_,
        .conversation = kcp_.conversation(),
        .remote_endpoint = remote_endpoint_,
        .session_state = session_state_,
        .route_state = route_state_,
        .route_target = route_target_,
        .connected_at_unix_ms = connected_at_unix_ms_,
        .authenticated_at_unix_ms = authenticated_at_unix_ms_,
        .last_active_unix_ms = last_active_unix_ms_,
        .closed_at_unix_ms = closed_at_unix_ms_,
        .close_reason_code = close_reason_code_,
    };
}

std::string_view ClientSession::last_error_message() const noexcept
{
    return last_error_message_;
}

ClientSessionErrorCode ClientSession::BeginAuthentication() noexcept
{
    if (!valid_)
    {
        return SetError(ClientSessionErrorCode::InvalidState);
    }

    if (!CanTransitionTo(ClientSessionState::Authenticating))
    {
        return SetError(
            ClientSessionErrorCode::InvalidSessionStateTransition,
            "Client session cannot transition from " +
                std::string(ClientSessionStateName(session_state_)) +
                " to Authenticating.");
    }

    session_state_ = ClientSessionState::Authenticating;
    ClearError();
    return ClientSessionErrorCode::None;
}

ClientSessionErrorCode ClientSession::Activate(std::uint64_t authenticated_at_unix_ms) noexcept
{
    if (!valid_)
    {
        return SetError(ClientSessionErrorCode::InvalidState);
    }

    if (!CanTransitionTo(ClientSessionState::Active))
    {
        return SetError(
            ClientSessionErrorCode::InvalidSessionStateTransition,
            "Client session cannot transition from " +
                std::string(ClientSessionStateName(session_state_)) +
                " to Active.");
    }

    if (authenticated_at_unix_ms != 0U)
    {
        authenticated_at_unix_ms_ = authenticated_at_unix_ms;
    }

    session_state_ = ClientSessionState::Active;
    ClearError();
    return ClientSessionErrorCode::None;
}

ClientSessionErrorCode ClientSession::BeginClosing() noexcept
{
    if (!valid_)
    {
        return SetError(ClientSessionErrorCode::InvalidState);
    }

    if (!CanTransitionTo(ClientSessionState::Closing))
    {
        return SetError(
            ClientSessionErrorCode::InvalidSessionStateTransition,
            "Client session cannot transition from " +
                std::string(ClientSessionStateName(session_state_)) +
                " to Closing.");
    }

    session_state_ = ClientSessionState::Closing;
    ClearError();
    return ClientSessionErrorCode::None;
}

ClientSessionErrorCode ClientSession::Close(
    std::int32_t close_reason_code,
    std::uint64_t closed_at_unix_ms) noexcept
{
    if (!valid_)
    {
        return SetError(ClientSessionErrorCode::InvalidState);
    }

    if (closed_at_unix_ms == 0U)
    {
        return SetError(
            ClientSessionErrorCode::InvalidArgument,
            "Client session closed_at_unix_ms must be greater than zero.");
    }

    if (!CanTransitionTo(ClientSessionState::Closed))
    {
        return SetError(
            ClientSessionErrorCode::InvalidSessionStateTransition,
            "Client session cannot transition from " +
                std::string(ClientSessionStateName(session_state_)) +
                " to Closed.");
    }

    close_reason_code_ = close_reason_code;
    closed_at_unix_ms_ = closed_at_unix_ms;
    session_state_ = ClientSessionState::Closed;
    ClearError();
    return ClientSessionErrorCode::None;
}

ClientSessionErrorCode ClientSession::SetRouteSelecting() noexcept
{
    if (!valid_)
    {
        return SetError(ClientSessionErrorCode::InvalidState);
    }

    if (!CanTransitionRouteTo(ClientRouteState::Selecting))
    {
        return SetError(
            ClientSessionErrorCode::InvalidRouteStateTransition,
            "Client session cannot transition route state from " +
                std::string(ClientRouteStateName(route_state_)) +
                " to Selecting.");
    }

    route_state_ = ClientRouteState::Selecting;
    route_target_.reset();
    ClearError();
    return ClientSessionErrorCode::None;
}

ClientSessionErrorCode ClientSession::BindRoute(ClientRouteTarget route_target)
{
    if (!valid_)
    {
        return SetError(ClientSessionErrorCode::InvalidState);
    }

    if (route_target.game_node_id.empty() ||
        route_target.inner_network_endpoint.host.empty() ||
        route_target.inner_network_endpoint.port == 0U ||
        route_target.route_epoch == 0U)
    {
        return SetError(
            ClientSessionErrorCode::InvalidRouteTarget,
            "Client route target must include nodeId, endpoint, and routeEpoch.");
    }

    if (!CanTransitionRouteTo(ClientRouteState::Bound))
    {
        return SetError(
            ClientSessionErrorCode::InvalidRouteStateTransition,
            "Client session cannot transition route state from " +
                std::string(ClientRouteStateName(route_state_)) +
                " to Bound.");
    }

    route_target_ = std::move(route_target);
    route_state_ = ClientRouteState::Bound;
    ClearError();
    return ClientSessionErrorCode::None;
}

ClientSessionErrorCode ClientSession::MarkRouteLost() noexcept
{
    if (!valid_)
    {
        return SetError(ClientSessionErrorCode::InvalidState);
    }

    if (!CanTransitionRouteTo(ClientRouteState::RouteLost))
    {
        return SetError(
            ClientSessionErrorCode::InvalidRouteStateTransition,
            "Client session cannot transition route state from " +
                std::string(ClientRouteStateName(route_state_)) +
                " to RouteLost.");
    }

    route_state_ = ClientRouteState::RouteLost;
    ClearError();
    return ClientSessionErrorCode::None;
}

ClientSessionErrorCode ClientSession::ReleaseRoute() noexcept
{
    if (!valid_)
    {
        return SetError(ClientSessionErrorCode::InvalidState);
    }

    if (!CanTransitionRouteTo(ClientRouteState::Released))
    {
        return SetError(
            ClientSessionErrorCode::InvalidRouteStateTransition,
            "Client session cannot transition route state from " +
                std::string(ClientRouteStateName(route_state_)) +
                " to Released.");
    }

    route_target_.reset();
    route_state_ = ClientRouteState::Released;
    ClearError();
    return ClientSessionErrorCode::None;
}

void ClientSession::Touch(std::uint64_t last_active_unix_ms) noexcept
{
    if (valid_ && last_active_unix_ms != 0U)
    {
        last_active_unix_ms_ = last_active_unix_ms;
    }
}

ClientSessionErrorCode ClientSession::SetError(ClientSessionErrorCode code, std::string message)
{
    if (message.empty())
    {
        last_error_message_ = std::string(ClientSessionErrorMessage(code));
    }
    else
    {
        last_error_message_ = std::move(message);
    }

    return code;
}

void ClientSession::ClearError() noexcept
{
    last_error_message_.clear();
}

bool ClientSession::CanTransitionTo(ClientSessionState next_state) const noexcept
{
    if (session_state_ == next_state)
    {
        return true;
    }

    switch (session_state_)
    {
    case ClientSessionState::Created:
        return next_state == ClientSessionState::Authenticating ||
               next_state == ClientSessionState::Active ||
               next_state == ClientSessionState::Closing ||
               next_state == ClientSessionState::Closed;
    case ClientSessionState::Authenticating:
        return next_state == ClientSessionState::Active ||
               next_state == ClientSessionState::Closing ||
               next_state == ClientSessionState::Closed;
    case ClientSessionState::Active:
        return next_state == ClientSessionState::Closing ||
               next_state == ClientSessionState::Closed;
    case ClientSessionState::Closing:
        return next_state == ClientSessionState::Closed;
    case ClientSessionState::Closed:
        return false;
    }

    return false;
}

bool ClientSession::CanTransitionRouteTo(ClientRouteState next_state) const noexcept
{
    if (route_state_ == next_state)
    {
        return true;
    }

    switch (route_state_)
    {
    case ClientRouteState::Unassigned:
        return next_state == ClientRouteState::Selecting ||
               next_state == ClientRouteState::Bound ||
               next_state == ClientRouteState::Released;
    case ClientRouteState::Selecting:
        return next_state == ClientRouteState::Unassigned ||
               next_state == ClientRouteState::Bound ||
               next_state == ClientRouteState::Released;
    case ClientRouteState::Bound:
        return next_state == ClientRouteState::RouteLost ||
               next_state == ClientRouteState::Released;
    case ClientRouteState::RouteLost:
        return next_state == ClientRouteState::Bound ||
               next_state == ClientRouteState::Released;
    case ClientRouteState::Released:
        return false;
    }

    return false;
}

} // namespace xs::node

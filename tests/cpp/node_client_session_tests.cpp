#include "ClientSession.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace
{

int g_failures = 0;

void Check(bool condition, const char* expression, const char* message = nullptr)
{
    if (condition)
    {
        return;
    }

    std::cerr << "Check failed: " << expression;
    if (message != nullptr)
    {
        std::cerr << " (" << message << ")";
    }
    std::cerr << '\n';
    ++g_failures;
}

#define XS_CHECK(expr) Check((expr), #expr)
#define XS_CHECK_MSG(expr, message) Check((expr), #expr, (message))

xs::net::Endpoint MakeEndpoint(std::string host, std::uint16_t port)
{
    return xs::net::Endpoint{
        .host = std::move(host),
        .port = port,
    };
}

void TestClientSessionDefaultsMatchSessionRoutingModel()
{
    xs::node::ClientSession session({
        .gate_node_id = "Gate0",
        .session_id = 11U,
        .conversation = 77U,
        .remote_endpoint = MakeEndpoint("127.0.0.1", 50000U),
        .kcp = {},
        .connected_at_unix_ms = 12345U,
    });
    XS_CHECK_MSG(session.valid(), session.last_error_message().data());

    const xs::node::ClientSessionSnapshot snapshot = session.snapshot();
    XS_CHECK(snapshot.gate_node_id == "Gate0");
    XS_CHECK(snapshot.session_id == 11U);
    XS_CHECK(snapshot.conversation == 77U);
    XS_CHECK(snapshot.remote_endpoint.host == "127.0.0.1");
    XS_CHECK(snapshot.remote_endpoint.port == 50000U);
    XS_CHECK(snapshot.session_state == xs::node::ClientSessionState::Created);
    XS_CHECK(snapshot.route_state == xs::node::ClientRouteState::Unassigned);
    XS_CHECK(snapshot.player_id == 0U);
    XS_CHECK(!snapshot.route_target.has_value());
    XS_CHECK(snapshot.connected_at_unix_ms == 12345U);
    XS_CHECK(snapshot.authenticated_at_unix_ms == 0U);
    XS_CHECK(snapshot.last_active_unix_ms == 0U);
    XS_CHECK(snapshot.closed_at_unix_ms == 0U);
    XS_CHECK(snapshot.close_reason_code == 0);
}

void TestClientSessionSupportsExpectedStateAndRouteTransitions()
{
    xs::node::ClientSession session({
        .gate_node_id = "Gate0",
        .session_id = 12U,
        .conversation = 88U,
        .remote_endpoint = MakeEndpoint("127.0.0.1", 50001U),
        .kcp = {},
        .connected_at_unix_ms = 100U,
    });
    XS_CHECK_MSG(session.valid(), session.last_error_message().data());

    XS_CHECK(session.BeginAuthentication() == xs::node::ClientSessionErrorCode::None);
    XS_CHECK(session.Activate(200U, 9001U) == xs::node::ClientSessionErrorCode::None);
    session.Touch(250U);
    XS_CHECK(session.SetRouteSelecting() == xs::node::ClientSessionErrorCode::None);
    XS_CHECK(
        session.BindRoute({
            .game_node_id = "Game0",
            .inner_network_endpoint = MakeEndpoint("127.0.0.1", 7100U),
            .route_epoch = 3U,
        }) == xs::node::ClientSessionErrorCode::None);
    XS_CHECK(session.MarkRouteLost() == xs::node::ClientSessionErrorCode::None);
    XS_CHECK(session.ReleaseRoute() == xs::node::ClientSessionErrorCode::None);
    XS_CHECK(session.BeginClosing() == xs::node::ClientSessionErrorCode::None);
    XS_CHECK(session.Close(2001, 300U) == xs::node::ClientSessionErrorCode::None);

    const xs::node::ClientSessionSnapshot snapshot = session.snapshot();
    XS_CHECK(snapshot.session_state == xs::node::ClientSessionState::Closed);
    XS_CHECK(snapshot.route_state == xs::node::ClientRouteState::Released);
    XS_CHECK(snapshot.player_id == 9001U);
    XS_CHECK(!snapshot.route_target.has_value());
    XS_CHECK(snapshot.authenticated_at_unix_ms == 200U);
    XS_CHECK(snapshot.last_active_unix_ms == 250U);
    XS_CHECK(snapshot.closed_at_unix_ms == 300U);
    XS_CHECK(snapshot.close_reason_code == 2001);
}

void TestClientSessionRejectsInvalidTransitionsAndRouteTargets()
{
    xs::node::ClientSession session({
        .gate_node_id = "Gate0",
        .session_id = 13U,
        .conversation = 99U,
        .remote_endpoint = MakeEndpoint("127.0.0.1", 50002U),
        .kcp = {},
        .connected_at_unix_ms = 150U,
    });
    XS_CHECK_MSG(session.valid(), session.last_error_message().data());

    XS_CHECK(
        session.BindRoute({
            .game_node_id = "",
            .inner_network_endpoint = MakeEndpoint("127.0.0.1", 7101U),
            .route_epoch = 1U,
        }) == xs::node::ClientSessionErrorCode::InvalidRouteTarget);
    XS_CHECK(session.BeginClosing() == xs::node::ClientSessionErrorCode::None);
    XS_CHECK(session.Close(2002, 400U) == xs::node::ClientSessionErrorCode::None);
    XS_CHECK(session.BeginAuthentication() == xs::node::ClientSessionErrorCode::InvalidSessionStateTransition);
    XS_CHECK(std::string(session.last_error_message()).find("Closed") != std::string::npos);
}

} // namespace

int main()
{
    TestClientSessionDefaultsMatchSessionRoutingModel();
    TestClientSessionSupportsExpectedStateAndRouteTransitions();
    TestClientSessionRejectsInvalidTransitionsAndRouteTargets();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " client session test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

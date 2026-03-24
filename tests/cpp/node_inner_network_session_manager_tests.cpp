#include "InnerNetworkSessionManager.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

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

[[nodiscard]] xs::node::RoutingID MakeRoutingId(std::string_view value)
{
    xs::node::RoutingID routing_id;
    routing_id.reserve(value.size());

    for (const char ch : value)
    {
        routing_id.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }

    return routing_id;
}

[[nodiscard]] xs::net::Endpoint MakeEndpoint(std::string host, std::uint16_t port)
{
    return xs::net::Endpoint{
        .host = std::move(host),
        .port = port,
    };
}

[[nodiscard]] xs::net::LoadSnapshot MakeLoadSnapshot(
    std::uint32_t connection_count,
    std::uint32_t session_count,
    std::uint32_t entity_count,
    std::uint32_t space_count,
    std::uint32_t load_score)
{
    return xs::net::LoadSnapshot{
        .connection_count = connection_count,
        .session_count = session_count,
        .entity_count = entity_count,
        .space_count = space_count,
        .load_score = load_score,
    };
}

[[nodiscard]] xs::node::InnerNetworkSessionRegistration MakeRegistration(
    xs::core::ProcessType process_type,
    std::string node_id,
    std::string routing_id)
{
    return xs::node::InnerNetworkSessionRegistration{
        .process_type = process_type,
        .node_id = std::move(node_id),
        .pid = 101U,
        .started_at_unix_ms = 202U,
        .inner_network_endpoint = MakeEndpoint("127.0.0.1", 6000U),
        .build_version = "build-1",
        .capability_tags = {"inner", "route"},
        .load = MakeLoadSnapshot(1U, 2U, 3U, 4U, 5U),
        .routing_id = MakeRoutingId(routing_id),
        .last_heartbeat_at_unix_ms = 303U,
        .inner_network_ready = false,
    };
}

void CheckEntryFields(
    const xs::node::InnerNetworkSession& entry,
    xs::core::ProcessType process_type,
    std::string_view node_id,
    std::string_view routing_id)
{
    XS_CHECK(entry.process_type == process_type);
    XS_CHECK(entry.node_id == node_id);
    XS_CHECK(entry.pid == 101U);
    XS_CHECK(entry.started_at_unix_ms == 202U);
    XS_CHECK(entry.inner_network_endpoint.host == "127.0.0.1");
    XS_CHECK(entry.inner_network_endpoint.port == 6000U);
    XS_CHECK(entry.build_version == "build-1");
    XS_CHECK(entry.capability_tags.size() == 2U);
    XS_CHECK(entry.capability_tags[0] == "inner");
    XS_CHECK(entry.capability_tags[1] == "route");
    XS_CHECK(entry.load.connection_count == 1U);
    XS_CHECK(entry.load.session_count == 2U);
    XS_CHECK(entry.load.entity_count == 3U);
    XS_CHECK(entry.load.space_count == 4U);
    XS_CHECK(entry.load.load_score == 5U);
    XS_CHECK(entry.last_heartbeat_at_unix_ms == 303U);
    XS_CHECK(!entry.inner_network_ready);
    XS_CHECK(entry.routing_id == MakeRoutingId(routing_id));
}

void TestRegisterAndSnapshotSortByNodeId()
{
    xs::node::InnerNetworkSessionManager manager;

    const xs::node::InnerNetworkSessionRegistration game_registration =
        MakeRegistration(xs::core::ProcessType::Game, "Game1", "route-game");
    const xs::node::InnerNetworkSessionRegistration gate_registration =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "route-gate");

    XS_CHECK(manager.Register(game_registration) == xs::node::InnerNetworkSessionManagerErrorCode::None);
    XS_CHECK(manager.Register(gate_registration) == xs::node::InnerNetworkSessionManagerErrorCode::None);
    XS_CHECK(manager.size() == 2U);
    XS_CHECK(manager.ContainsNodeId("Game1"));
    XS_CHECK(manager.ContainsNodeId("Gate0"));
    XS_CHECK(manager.ContainsRoutingId(MakeRoutingId("route-game")));
    XS_CHECK(manager.ContainsRoutingId(MakeRoutingId("route-gate")));

    const xs::node::InnerNetworkSession* gate_entry = manager.FindByNodeId("Gate0");
    XS_CHECK(gate_entry != nullptr);
    if (gate_entry != nullptr)
    {
        CheckEntryFields(*gate_entry, xs::core::ProcessType::Gate, "Gate0", "route-gate");
    }

    const xs::node::InnerNetworkSession* game_entry = manager.FindByRoutingId(MakeRoutingId("route-game"));
    XS_CHECK(game_entry != nullptr);
    if (game_entry != nullptr)
    {
        CheckEntryFields(*game_entry, xs::core::ProcessType::Game, "Game1", "route-game");
    }

    const std::vector<xs::node::InnerNetworkSession> snapshot = manager.Snapshot();
    XS_CHECK(snapshot.size() == 2U);
    XS_CHECK(snapshot[0].node_id == "Game1");
    XS_CHECK(snapshot[1].node_id == "Gate0");
}

void TestRejectsInvalidRegistrationsAndConflicts()
{
    xs::node::InnerNetworkSessionManager manager;

    xs::node::InnerNetworkSessionRegistration invalid_process =
        MakeRegistration(static_cast<xs::core::ProcessType>(255U), "Gate0", "route-a");
    XS_CHECK(
        manager.Register(invalid_process) ==
        xs::node::InnerNetworkSessionManagerErrorCode::InvalidProcessType);

    xs::node::InnerNetworkSessionRegistration invalid_node =
        MakeRegistration(xs::core::ProcessType::Gate, "", "route-a");
    XS_CHECK(manager.Register(invalid_node) == xs::node::InnerNetworkSessionManagerErrorCode::InvalidNodeId);

    xs::node::InnerNetworkSessionRegistration invalid_host =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "route-a");
    invalid_host.inner_network_endpoint.host.clear();
    XS_CHECK(
        manager.Register(invalid_host) ==
        xs::node::InnerNetworkSessionManagerErrorCode::InvalidInnerNetworkEndpointHost);

    xs::node::InnerNetworkSessionRegistration invalid_port =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "route-a");
    invalid_port.inner_network_endpoint.port = 0U;
    XS_CHECK(
        manager.Register(invalid_port) ==
        xs::node::InnerNetworkSessionManagerErrorCode::InvalidInnerNetworkEndpointPort);

    const xs::node::InnerNetworkSessionRegistration gate_registration =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "route-a");
    XS_CHECK(manager.Register(gate_registration) == xs::node::InnerNetworkSessionManagerErrorCode::None);

    const xs::node::InnerNetworkSessionRegistration duplicate_node =
        MakeRegistration(xs::core::ProcessType::Game, "Gate0", "route-b");
    XS_CHECK(manager.Register(duplicate_node) == xs::node::InnerNetworkSessionManagerErrorCode::NodeIdConflict);

    const xs::node::InnerNetworkSessionRegistration duplicate_routing =
        MakeRegistration(xs::core::ProcessType::Game, "Game0", "route-a");
    XS_CHECK(
        manager.Register(duplicate_routing) ==
        xs::node::InnerNetworkSessionManagerErrorCode::RoutingIdConflict);
}

void TestUpdatesAndRemovesByNodeIdAndRoutingId()
{
    xs::node::InnerNetworkSessionManager manager;
    const xs::node::InnerNetworkSessionRegistration registration =
        MakeRegistration(xs::core::ProcessType::Game, "Game0", "route-game");

    XS_CHECK(manager.Register(registration) == xs::node::InnerNetworkSessionManagerErrorCode::None);

    const xs::net::LoadSnapshot updated_load = MakeLoadSnapshot(8U, 9U, 10U, 11U, 12U);
    XS_CHECK(
        manager.UpdateHeartbeatByNodeId("Game0", 999U, updated_load) ==
        xs::node::InnerNetworkSessionManagerErrorCode::None);
    XS_CHECK(
        manager.UpdateInnerNetworkReadyByRoutingId(MakeRoutingId("route-game"), true) ==
        xs::node::InnerNetworkSessionManagerErrorCode::None);

    const xs::node::InnerNetworkSession* entry = manager.FindByNodeId("Game0");
    XS_CHECK(entry != nullptr);
    if (entry != nullptr)
    {
        XS_CHECK(entry->last_heartbeat_at_unix_ms == 999U);
        XS_CHECK(entry->load.connection_count == 8U);
        XS_CHECK(entry->load.session_count == 9U);
        XS_CHECK(entry->load.entity_count == 10U);
        XS_CHECK(entry->load.space_count == 11U);
        XS_CHECK(entry->load.load_score == 12U);
        XS_CHECK(entry->inner_network_ready);
    }

    XS_CHECK(
        manager.UpdateHeartbeatByRoutingId(MakeRoutingId("missing"), 1U, updated_load) ==
        xs::node::InnerNetworkSessionManagerErrorCode::RoutingIdNotFound);
    XS_CHECK(
        manager.UpdateInnerNetworkReadyByNodeId("Missing", true) ==
        xs::node::InnerNetworkSessionManagerErrorCode::NodeNotFound);
    XS_CHECK(
        manager.UnregisterByRoutingId(MakeRoutingId("route-game")) ==
        xs::node::InnerNetworkSessionManagerErrorCode::None);
    XS_CHECK(manager.size() == 0U);
    XS_CHECK(!manager.ContainsNodeId("Game0"));

    XS_CHECK(manager.UnregisterByNodeId("Game0") == xs::node::InnerNetworkSessionManagerErrorCode::NodeNotFound);
}

void TestClearAndErrorMessages()
{
    xs::node::InnerNetworkSessionManager manager;
    XS_CHECK(manager.Snapshot().empty());

    XS_CHECK(
        xs::node::InnerNetworkSessionManagerErrorMessage(
            xs::node::InnerNetworkSessionManagerErrorCode::RoutingIdNotFound) ==
        std::string_view("Inner network session manager session was not found for the routingId."));

    const xs::node::InnerNetworkSessionRegistration gate_registration =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "");
    XS_CHECK(manager.Register(gate_registration) == xs::node::InnerNetworkSessionManagerErrorCode::None);
    XS_CHECK(!manager.ContainsRoutingId(MakeRoutingId("")));
    manager.Clear();
    XS_CHECK(manager.size() == 0U);
    XS_CHECK(manager.Snapshot().empty());
}

} // namespace

int main()
{
    TestRegisterAndSnapshotSortByNodeId();
    TestRejectsInvalidRegistrationsAndConflicts();
    TestUpdatesAndRemovesByNodeIdAndRoutingId();
    TestClearAndErrorMessages();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " inner network session manager test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

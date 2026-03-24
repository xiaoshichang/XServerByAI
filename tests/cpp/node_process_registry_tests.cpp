#include "ProcessRegistry.h"

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

[[nodiscard]] xs::node::ProcessRegistryRegistration MakeRegistration(
    xs::core::ProcessType process_type,
    std::string node_id,
    std::string routing_id)
{
    return xs::node::ProcessRegistryRegistration{
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
    const xs::node::ProcessRegistryEntry& entry,
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
    xs::node::ProcessRegistry registry;

    const xs::node::ProcessRegistryRegistration game_registration =
        MakeRegistration(xs::core::ProcessType::Game, "Game1", "route-game");
    const xs::node::ProcessRegistryRegistration gate_registration =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "route-gate");

    XS_CHECK(registry.Register(game_registration) == xs::node::ProcessRegistryErrorCode::None);
    XS_CHECK(registry.Register(gate_registration) == xs::node::ProcessRegistryErrorCode::None);
    XS_CHECK(registry.size() == 2U);
    XS_CHECK(registry.ContainsNodeId("Game1"));
    XS_CHECK(registry.ContainsNodeId("Gate0"));
    XS_CHECK(registry.ContainsRoutingId(MakeRoutingId("route-game")));
    XS_CHECK(registry.ContainsRoutingId(MakeRoutingId("route-gate")));

    const xs::node::ProcessRegistryEntry* gate_entry = registry.FindByNodeId("Gate0");
    XS_CHECK(gate_entry != nullptr);
    if (gate_entry != nullptr)
    {
        CheckEntryFields(*gate_entry, xs::core::ProcessType::Gate, "Gate0", "route-gate");
    }

    const xs::node::ProcessRegistryEntry* game_entry = registry.FindByRoutingId(MakeRoutingId("route-game"));
    XS_CHECK(game_entry != nullptr);
    if (game_entry != nullptr)
    {
        CheckEntryFields(*game_entry, xs::core::ProcessType::Game, "Game1", "route-game");
    }

    const std::vector<xs::node::ProcessRegistryEntry> snapshot = registry.Snapshot();
    XS_CHECK(snapshot.size() == 2U);
    XS_CHECK(snapshot[0].node_id == "Game1");
    XS_CHECK(snapshot[1].node_id == "Gate0");
}

void TestRejectsInvalidRegistrationsAndConflicts()
{
    xs::node::ProcessRegistry registry;

    xs::node::ProcessRegistryRegistration invalid_process =
        MakeRegistration(static_cast<xs::core::ProcessType>(255U), "Gate0", "route-a");
    XS_CHECK(registry.Register(invalid_process) == xs::node::ProcessRegistryErrorCode::InvalidProcessType);

    xs::node::ProcessRegistryRegistration invalid_node =
        MakeRegistration(xs::core::ProcessType::Gate, "", "route-a");
    XS_CHECK(registry.Register(invalid_node) == xs::node::ProcessRegistryErrorCode::InvalidNodeId);

    xs::node::ProcessRegistryRegistration invalid_host =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "route-a");
    invalid_host.inner_network_endpoint.host.clear();
    XS_CHECK(registry.Register(invalid_host) == xs::node::ProcessRegistryErrorCode::InvalidInnerNetworkEndpointHost);

    xs::node::ProcessRegistryRegistration invalid_port =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "route-a");
    invalid_port.inner_network_endpoint.port = 0U;
    XS_CHECK(registry.Register(invalid_port) == xs::node::ProcessRegistryErrorCode::InvalidInnerNetworkEndpointPort);

    const xs::node::ProcessRegistryRegistration gate_registration =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "route-a");
    XS_CHECK(registry.Register(gate_registration) == xs::node::ProcessRegistryErrorCode::None);

    const xs::node::ProcessRegistryRegistration duplicate_node =
        MakeRegistration(xs::core::ProcessType::Game, "Gate0", "route-b");
    XS_CHECK(registry.Register(duplicate_node) == xs::node::ProcessRegistryErrorCode::NodeIdConflict);

    const xs::node::ProcessRegistryRegistration duplicate_routing =
        MakeRegistration(xs::core::ProcessType::Game, "Game0", "route-a");
    XS_CHECK(registry.Register(duplicate_routing) == xs::node::ProcessRegistryErrorCode::RoutingIdConflict);
}

void TestUpdatesAndRemovesByNodeIdAndRoutingId()
{
    xs::node::ProcessRegistry registry;
    const xs::node::ProcessRegistryRegistration registration =
        MakeRegistration(xs::core::ProcessType::Game, "Game0", "route-game");

    XS_CHECK(registry.Register(registration) == xs::node::ProcessRegistryErrorCode::None);

    const xs::net::LoadSnapshot updated_load = MakeLoadSnapshot(8U, 9U, 10U, 11U, 12U);
    XS_CHECK(
        registry.UpdateHeartbeatByNodeId("Game0", 999U, updated_load) ==
        xs::node::ProcessRegistryErrorCode::None);
    XS_CHECK(
        registry.UpdateInnerNetworkReadyByRoutingId(MakeRoutingId("route-game"), true) ==
        xs::node::ProcessRegistryErrorCode::None);

    const xs::node::ProcessRegistryEntry* entry = registry.FindByNodeId("Game0");
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
        registry.UpdateHeartbeatByRoutingId(MakeRoutingId("missing"), 1U, updated_load) ==
        xs::node::ProcessRegistryErrorCode::RoutingIdNotFound);
    XS_CHECK(
        registry.UpdateInnerNetworkReadyByNodeId("Missing", true) ==
        xs::node::ProcessRegistryErrorCode::NodeNotFound);
    XS_CHECK(
        registry.UnregisterByRoutingId(MakeRoutingId("route-game")) ==
        xs::node::ProcessRegistryErrorCode::None);
    XS_CHECK(registry.size() == 0U);
    XS_CHECK(!registry.ContainsNodeId("Game0"));

    XS_CHECK(registry.UnregisterByNodeId("Game0") == xs::node::ProcessRegistryErrorCode::NodeNotFound);
}

void TestClearAndErrorMessages()
{
    xs::node::ProcessRegistry registry;
    XS_CHECK(registry.Snapshot().empty());

    XS_CHECK(
        xs::node::ProcessRegistryErrorMessage(xs::node::ProcessRegistryErrorCode::RoutingIdNotFound) ==
        std::string_view("Process registry entry was not found for the routingId."));

    const xs::node::ProcessRegistryRegistration gate_registration =
        MakeRegistration(xs::core::ProcessType::Gate, "Gate0", "");
    XS_CHECK(registry.Register(gate_registration) == xs::node::ProcessRegistryErrorCode::None);
    XS_CHECK(!registry.ContainsRoutingId(MakeRoutingId("")));
    registry.Clear();
    XS_CHECK(registry.size() == 0U);
    XS_CHECK(registry.Snapshot().empty());
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
        std::cerr << g_failures << " process registry test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

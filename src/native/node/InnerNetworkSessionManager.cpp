#include "InnerNetworkSessionManager.h"

#include <utility>

namespace xs::node
{
namespace
{

[[nodiscard]] std::string MakeRoutingKey(std::span<const std::byte> routing_id)
{
    return std::string(
        reinterpret_cast<const char*>(routing_id.data()),
        reinterpret_cast<const char*>(routing_id.data() + routing_id.size()));
}

[[nodiscard]] InnerNetworkSessionManagerErrorCode ValidateProcessType(xs::core::ProcessType process_type) noexcept
{
    switch (process_type)
    {
    case xs::core::ProcessType::Gm:
    case xs::core::ProcessType::Gate:
    case xs::core::ProcessType::Game:
        return InnerNetworkSessionManagerErrorCode::None;
    }

    return InnerNetworkSessionManagerErrorCode::InvalidProcessType;
}

[[nodiscard]] InnerNetworkSessionManagerErrorCode ValidateNodeId(std::string_view node_id) noexcept
{
    if (node_id.empty())
    {
        return InnerNetworkSessionManagerErrorCode::InvalidNodeId;
    }

    return InnerNetworkSessionManagerErrorCode::None;
}

[[nodiscard]] InnerNetworkSessionManagerErrorCode ValidateEndpoint(const xs::net::Endpoint& endpoint) noexcept
{
    if (endpoint.host.empty())
    {
        return InnerNetworkSessionManagerErrorCode::InvalidInnerNetworkEndpointHost;
    }

    if (endpoint.port == 0U)
    {
        return InnerNetworkSessionManagerErrorCode::InvalidInnerNetworkEndpointPort;
    }

    return InnerNetworkSessionManagerErrorCode::None;
}

[[nodiscard]] InnerNetworkSessionManagerErrorCode ValidateRoutingId(std::span<const std::byte> routing_id) noexcept
{
    if (routing_id.empty())
    {
        return InnerNetworkSessionManagerErrorCode::InvalidArgument;
    }

    return InnerNetworkSessionManagerErrorCode::None;
}

[[nodiscard]] InnerNetworkSessionManagerErrorCode ValidateRegistration(
    const InnerNetworkSessionRegistration& registration) noexcept
{
    const InnerNetworkSessionManagerErrorCode process_type_result = ValidateProcessType(registration.process_type);
    if (process_type_result != InnerNetworkSessionManagerErrorCode::None)
    {
        return process_type_result;
    }

    const InnerNetworkSessionManagerErrorCode node_id_result = ValidateNodeId(registration.node_id);
    if (node_id_result != InnerNetworkSessionManagerErrorCode::None)
    {
        return node_id_result;
    }

    return ValidateEndpoint(registration.inner_network_endpoint);
}

} // namespace

std::string_view InnerNetworkSessionManagerErrorMessage(InnerNetworkSessionManagerErrorCode code) noexcept
{
    switch (code)
    {
    case InnerNetworkSessionManagerErrorCode::None:
        return "Success.";
    case InnerNetworkSessionManagerErrorCode::InvalidArgument:
        return "Inner network session manager argument is invalid.";
    case InnerNetworkSessionManagerErrorCode::InvalidProcessType:
        return "Inner network session manager only supports GM, Gate, or Game sessions.";
    case InnerNetworkSessionManagerErrorCode::InvalidNodeId:
        return "Inner network session manager nodeId must not be empty.";
    case InnerNetworkSessionManagerErrorCode::InvalidInnerNetworkEndpointHost:
        return "Inner network session manager innerNetworkEndpoint.host must not be empty.";
    case InnerNetworkSessionManagerErrorCode::InvalidInnerNetworkEndpointPort:
        return "Inner network session manager innerNetworkEndpoint.port must not be zero.";
    case InnerNetworkSessionManagerErrorCode::NodeIdConflict:
        return "Inner network session manager already contains an active session for the nodeId.";
    case InnerNetworkSessionManagerErrorCode::RoutingIdConflict:
        return "Inner network session manager already contains an active session for the routingId.";
    case InnerNetworkSessionManagerErrorCode::NodeNotFound:
        return "Inner network session manager session was not found for the nodeId.";
    case InnerNetworkSessionManagerErrorCode::RoutingIdNotFound:
        return "Inner network session manager session was not found for the routingId.";
    }

    return "Unknown inner network session manager error.";
}

InnerNetworkSessionManagerErrorCode InnerNetworkSessionManager::Register(
    const InnerNetworkSessionRegistration& registration)
{
    const InnerNetworkSessionManagerErrorCode validation_result = ValidateRegistration(registration);
    if (validation_result != InnerNetworkSessionManagerErrorCode::None)
    {
        return validation_result;
    }

    if (entries_by_node_id_.contains(registration.node_id))
    {
        return InnerNetworkSessionManagerErrorCode::NodeIdConflict;
    }

    if (!registration.routing_id.empty())
    {
        const std::string routing_key = MakeRoutingKey(registration.routing_id);
        if (node_id_by_routing_key_.contains(routing_key))
        {
            return InnerNetworkSessionManagerErrorCode::RoutingIdConflict;
        }
    }

    InnerNetworkSession entry;
    entry.process_type = registration.process_type;
    entry.node_id = registration.node_id;
    entry.pid = registration.pid;
    entry.started_at_unix_ms = registration.started_at_unix_ms;
    entry.inner_network_endpoint = registration.inner_network_endpoint;
    entry.build_version = registration.build_version;
    entry.capability_tags = registration.capability_tags;
    entry.load = registration.load;
    entry.routing_id = registration.routing_id;
    entry.last_heartbeat_at_unix_ms = registration.last_heartbeat_at_unix_ms;
    entry.inner_network_ready = registration.inner_network_ready;

    const auto [iterator, inserted] = entries_by_node_id_.emplace(entry.node_id, std::move(entry));
    if (!inserted)
    {
        return InnerNetworkSessionManagerErrorCode::NodeIdConflict;
    }

    if (!iterator->second.routing_id.empty())
    {
        node_id_by_routing_key_.emplace(MakeRoutingKey(iterator->second.routing_id), iterator->first);
    }

    return InnerNetworkSessionManagerErrorCode::None;
}

InnerNetworkSessionManagerErrorCode InnerNetworkSessionManager::UnregisterByNodeId(std::string_view node_id)
{
    const InnerNetworkSessionManagerErrorCode node_id_result = ValidateNodeId(node_id);
    if (node_id_result != InnerNetworkSessionManagerErrorCode::None)
    {
        return node_id_result;
    }

    auto iterator = entries_by_node_id_.find(node_id);
    if (iterator == entries_by_node_id_.end())
    {
        return InnerNetworkSessionManagerErrorCode::NodeNotFound;
    }

    if (!iterator->second.routing_id.empty())
    {
        node_id_by_routing_key_.erase(MakeRoutingKey(iterator->second.routing_id));
    }

    entries_by_node_id_.erase(iterator);
    return InnerNetworkSessionManagerErrorCode::None;
}

InnerNetworkSessionManagerErrorCode InnerNetworkSessionManager::UnregisterByRoutingId(
    std::span<const std::byte> routing_id)
{
    const InnerNetworkSessionManagerErrorCode routing_id_result = ValidateRoutingId(routing_id);
    if (routing_id_result != InnerNetworkSessionManagerErrorCode::None)
    {
        return routing_id_result;
    }

    const auto routing_iterator = node_id_by_routing_key_.find(MakeRoutingKey(routing_id));
    if (routing_iterator == node_id_by_routing_key_.end())
    {
        return InnerNetworkSessionManagerErrorCode::RoutingIdNotFound;
    }

    auto entry_iterator = entries_by_node_id_.find(routing_iterator->second);
    if (entry_iterator == entries_by_node_id_.end())
    {
        node_id_by_routing_key_.erase(routing_iterator);
        return InnerNetworkSessionManagerErrorCode::RoutingIdNotFound;
    }

    node_id_by_routing_key_.erase(routing_iterator);
    entries_by_node_id_.erase(entry_iterator);
    return InnerNetworkSessionManagerErrorCode::None;
}

InnerNetworkSessionManagerErrorCode InnerNetworkSessionManager::UpdateHeartbeatByNodeId(
    std::string_view node_id,
    std::uint64_t last_heartbeat_at_unix_ms,
    const xs::net::LoadSnapshot& load)
{
    InnerNetworkSession* entry = FindMutableByNodeId(node_id);
    if (entry == nullptr)
    {
        return node_id.empty() ? InnerNetworkSessionManagerErrorCode::InvalidNodeId
                               : InnerNetworkSessionManagerErrorCode::NodeNotFound;
    }

    entry->last_heartbeat_at_unix_ms = last_heartbeat_at_unix_ms;
    entry->load = load;
    return InnerNetworkSessionManagerErrorCode::None;
}

InnerNetworkSessionManagerErrorCode InnerNetworkSessionManager::UpdateHeartbeatByRoutingId(
    std::span<const std::byte> routing_id,
    std::uint64_t last_heartbeat_at_unix_ms,
    const xs::net::LoadSnapshot& load)
{
    InnerNetworkSession* entry = FindMutableByRoutingId(routing_id);
    if (entry == nullptr)
    {
        return routing_id.empty() ? InnerNetworkSessionManagerErrorCode::InvalidArgument
                                  : InnerNetworkSessionManagerErrorCode::RoutingIdNotFound;
    }

    entry->last_heartbeat_at_unix_ms = last_heartbeat_at_unix_ms;
    entry->load = load;
    return InnerNetworkSessionManagerErrorCode::None;
}

InnerNetworkSessionManagerErrorCode InnerNetworkSessionManager::UpdateInnerNetworkReadyByNodeId(
    std::string_view node_id,
    bool inner_network_ready)
{
    InnerNetworkSession* entry = FindMutableByNodeId(node_id);
    if (entry == nullptr)
    {
        return node_id.empty() ? InnerNetworkSessionManagerErrorCode::InvalidNodeId
                               : InnerNetworkSessionManagerErrorCode::NodeNotFound;
    }

    entry->inner_network_ready = inner_network_ready;
    return InnerNetworkSessionManagerErrorCode::None;
}

InnerNetworkSessionManagerErrorCode InnerNetworkSessionManager::UpdateInnerNetworkReadyByRoutingId(
    std::span<const std::byte> routing_id,
    bool inner_network_ready)
{
    InnerNetworkSession* entry = FindMutableByRoutingId(routing_id);
    if (entry == nullptr)
    {
        return routing_id.empty() ? InnerNetworkSessionManagerErrorCode::InvalidArgument
                                  : InnerNetworkSessionManagerErrorCode::RoutingIdNotFound;
    }

    entry->inner_network_ready = inner_network_ready;
    return InnerNetworkSessionManagerErrorCode::None;
}

const InnerNetworkSession* InnerNetworkSessionManager::FindByNodeId(std::string_view node_id) const
{
    auto iterator = entries_by_node_id_.find(node_id);
    if (iterator == entries_by_node_id_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const InnerNetworkSession* InnerNetworkSessionManager::FindByRoutingId(std::span<const std::byte> routing_id) const
{
    if (routing_id.empty())
    {
        return nullptr;
    }

    const auto routing_iterator = node_id_by_routing_key_.find(MakeRoutingKey(routing_id));
    if (routing_iterator == node_id_by_routing_key_.end())
    {
        return nullptr;
    }

    return FindByNodeId(routing_iterator->second);
}

bool InnerNetworkSessionManager::ContainsNodeId(std::string_view node_id) const
{
    return FindByNodeId(node_id) != nullptr;
}

bool InnerNetworkSessionManager::ContainsRoutingId(std::span<const std::byte> routing_id) const
{
    return FindByRoutingId(routing_id) != nullptr;
}

std::vector<InnerNetworkSession> InnerNetworkSessionManager::Snapshot() const
{
    std::vector<InnerNetworkSession> snapshot;
    snapshot.reserve(entries_by_node_id_.size());

    for (const auto& [node_id, entry] : entries_by_node_id_)
    {
        (void)node_id;
        snapshot.push_back(entry);
    }

    return snapshot;
}

std::size_t InnerNetworkSessionManager::size() const noexcept
{
    return entries_by_node_id_.size();
}

void InnerNetworkSessionManager::Clear() noexcept
{
    entries_by_node_id_.clear();
    node_id_by_routing_key_.clear();
}

InnerNetworkSession* InnerNetworkSessionManager::FindMutableByNodeId(std::string_view node_id)
{
    auto iterator = entries_by_node_id_.find(node_id);
    if (iterator == entries_by_node_id_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

InnerNetworkSession* InnerNetworkSessionManager::FindMutableByRoutingId(std::span<const std::byte> routing_id)
{
    if (routing_id.empty())
    {
        return nullptr;
    }

    const auto routing_iterator = node_id_by_routing_key_.find(MakeRoutingKey(routing_id));
    if (routing_iterator == node_id_by_routing_key_.end())
    {
        return nullptr;
    }

    return FindMutableByNodeId(routing_iterator->second);
}

} // namespace xs::node

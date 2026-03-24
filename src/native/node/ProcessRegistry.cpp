#include "ProcessRegistry.h"

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

[[nodiscard]] ProcessRegistryErrorCode ValidateProcessType(xs::core::ProcessType process_type) noexcept
{
    switch (process_type)
    {
    case xs::core::ProcessType::Gm:
    case xs::core::ProcessType::Gate:
    case xs::core::ProcessType::Game:
        return ProcessRegistryErrorCode::None;
    }

    return ProcessRegistryErrorCode::InvalidProcessType;
}

[[nodiscard]] ProcessRegistryErrorCode ValidateNodeId(std::string_view node_id) noexcept
{
    if (node_id.empty())
    {
        return ProcessRegistryErrorCode::InvalidNodeId;
    }

    return ProcessRegistryErrorCode::None;
}

[[nodiscard]] ProcessRegistryErrorCode ValidateEndpoint(const xs::net::Endpoint& endpoint) noexcept
{
    if (endpoint.host.empty())
    {
        return ProcessRegistryErrorCode::InvalidInnerNetworkEndpointHost;
    }

    if (endpoint.port == 0U)
    {
        return ProcessRegistryErrorCode::InvalidInnerNetworkEndpointPort;
    }

    return ProcessRegistryErrorCode::None;
}

[[nodiscard]] ProcessRegistryErrorCode ValidateRoutingId(std::span<const std::byte> routing_id) noexcept
{
    if (routing_id.empty())
    {
        return ProcessRegistryErrorCode::InvalidArgument;
    }

    return ProcessRegistryErrorCode::None;
}

[[nodiscard]] ProcessRegistryErrorCode ValidateRegistration(
    const InnerNetworkSessionRegistration& registration) noexcept
{
    const ProcessRegistryErrorCode process_type_result = ValidateProcessType(registration.process_type);
    if (process_type_result != ProcessRegistryErrorCode::None)
    {
        return process_type_result;
    }

    const ProcessRegistryErrorCode node_id_result = ValidateNodeId(registration.node_id);
    if (node_id_result != ProcessRegistryErrorCode::None)
    {
        return node_id_result;
    }

    return ValidateEndpoint(registration.inner_network_endpoint);
}

} // namespace

std::string_view ProcessRegistryErrorMessage(ProcessRegistryErrorCode code) noexcept
{
    switch (code)
    {
    case ProcessRegistryErrorCode::None:
        return "Success.";
    case ProcessRegistryErrorCode::InvalidArgument:
        return "Process registry argument is invalid.";
    case ProcessRegistryErrorCode::InvalidProcessType:
        return "Process registry only supports GM, Gate, or Game entries.";
    case ProcessRegistryErrorCode::InvalidNodeId:
        return "Process registry nodeId must not be empty.";
    case ProcessRegistryErrorCode::InvalidInnerNetworkEndpointHost:
        return "Process registry innerNetworkEndpoint.host must not be empty.";
    case ProcessRegistryErrorCode::InvalidInnerNetworkEndpointPort:
        return "Process registry innerNetworkEndpoint.port must not be zero.";
    case ProcessRegistryErrorCode::NodeIdConflict:
        return "Process registry already contains an active entry for the nodeId.";
    case ProcessRegistryErrorCode::RoutingIdConflict:
        return "Process registry already contains an active entry for the routingId.";
    case ProcessRegistryErrorCode::NodeNotFound:
        return "Process registry entry was not found for the nodeId.";
    case ProcessRegistryErrorCode::RoutingIdNotFound:
        return "Process registry entry was not found for the routingId.";
    }

    return "Unknown process registry error.";
}

ProcessRegistryErrorCode ProcessRegistry::Register(const InnerNetworkSessionRegistration& registration)
{
    const ProcessRegistryErrorCode validation_result = ValidateRegistration(registration);
    if (validation_result != ProcessRegistryErrorCode::None)
    {
        return validation_result;
    }

    if (entries_by_node_id_.contains(registration.node_id))
    {
        return ProcessRegistryErrorCode::NodeIdConflict;
    }

    if (!registration.routing_id.empty())
    {
        const std::string routing_key = MakeRoutingKey(registration.routing_id);
        if (node_id_by_routing_key_.contains(routing_key))
        {
            return ProcessRegistryErrorCode::RoutingIdConflict;
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
        return ProcessRegistryErrorCode::NodeIdConflict;
    }

    if (!iterator->second.routing_id.empty())
    {
        node_id_by_routing_key_.emplace(MakeRoutingKey(iterator->second.routing_id), iterator->first);
    }

    return ProcessRegistryErrorCode::None;
}

ProcessRegistryErrorCode ProcessRegistry::UnregisterByNodeId(std::string_view node_id)
{
    const ProcessRegistryErrorCode node_id_result = ValidateNodeId(node_id);
    if (node_id_result != ProcessRegistryErrorCode::None)
    {
        return node_id_result;
    }

    auto iterator = entries_by_node_id_.find(node_id);
    if (iterator == entries_by_node_id_.end())
    {
        return ProcessRegistryErrorCode::NodeNotFound;
    }

    if (!iterator->second.routing_id.empty())
    {
        node_id_by_routing_key_.erase(MakeRoutingKey(iterator->second.routing_id));
    }

    entries_by_node_id_.erase(iterator);
    return ProcessRegistryErrorCode::None;
}

ProcessRegistryErrorCode ProcessRegistry::UnregisterByRoutingId(std::span<const std::byte> routing_id)
{
    const ProcessRegistryErrorCode routing_id_result = ValidateRoutingId(routing_id);
    if (routing_id_result != ProcessRegistryErrorCode::None)
    {
        return routing_id_result;
    }

    const auto routing_iterator = node_id_by_routing_key_.find(MakeRoutingKey(routing_id));
    if (routing_iterator == node_id_by_routing_key_.end())
    {
        return ProcessRegistryErrorCode::RoutingIdNotFound;
    }

    auto entry_iterator = entries_by_node_id_.find(routing_iterator->second);
    if (entry_iterator == entries_by_node_id_.end())
    {
        node_id_by_routing_key_.erase(routing_iterator);
        return ProcessRegistryErrorCode::RoutingIdNotFound;
    }

    node_id_by_routing_key_.erase(routing_iterator);
    entries_by_node_id_.erase(entry_iterator);
    return ProcessRegistryErrorCode::None;
}

ProcessRegistryErrorCode ProcessRegistry::UpdateHeartbeatByNodeId(
    std::string_view node_id,
    std::uint64_t last_heartbeat_at_unix_ms,
    const xs::net::LoadSnapshot& load)
{
    ProcessRegistryEntry* entry = FindMutableByNodeId(node_id);
    if (entry == nullptr)
    {
        return node_id.empty() ? ProcessRegistryErrorCode::InvalidNodeId : ProcessRegistryErrorCode::NodeNotFound;
    }

    entry->last_heartbeat_at_unix_ms = last_heartbeat_at_unix_ms;
    entry->load = load;
    return ProcessRegistryErrorCode::None;
}

ProcessRegistryErrorCode ProcessRegistry::UpdateHeartbeatByRoutingId(
    std::span<const std::byte> routing_id,
    std::uint64_t last_heartbeat_at_unix_ms,
    const xs::net::LoadSnapshot& load)
{
    ProcessRegistryEntry* entry = FindMutableByRoutingId(routing_id);
    if (entry == nullptr)
    {
        return routing_id.empty() ? ProcessRegistryErrorCode::InvalidArgument
                                  : ProcessRegistryErrorCode::RoutingIdNotFound;
    }

    entry->last_heartbeat_at_unix_ms = last_heartbeat_at_unix_ms;
    entry->load = load;
    return ProcessRegistryErrorCode::None;
}

ProcessRegistryErrorCode ProcessRegistry::UpdateInnerNetworkReadyByNodeId(
    std::string_view node_id,
    bool inner_network_ready)
{
    ProcessRegistryEntry* entry = FindMutableByNodeId(node_id);
    if (entry == nullptr)
    {
        return node_id.empty() ? ProcessRegistryErrorCode::InvalidNodeId : ProcessRegistryErrorCode::NodeNotFound;
    }

    entry->inner_network_ready = inner_network_ready;
    return ProcessRegistryErrorCode::None;
}

ProcessRegistryErrorCode ProcessRegistry::UpdateInnerNetworkReadyByRoutingId(
    std::span<const std::byte> routing_id,
    bool inner_network_ready)
{
    ProcessRegistryEntry* entry = FindMutableByRoutingId(routing_id);
    if (entry == nullptr)
    {
        return routing_id.empty() ? ProcessRegistryErrorCode::InvalidArgument
                                  : ProcessRegistryErrorCode::RoutingIdNotFound;
    }

    entry->inner_network_ready = inner_network_ready;
    return ProcessRegistryErrorCode::None;
}

const InnerNetworkSession* ProcessRegistry::FindByNodeId(std::string_view node_id) const
{
    auto iterator = entries_by_node_id_.find(node_id);
    if (iterator == entries_by_node_id_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

const InnerNetworkSession* ProcessRegistry::FindByRoutingId(std::span<const std::byte> routing_id) const
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

bool ProcessRegistry::ContainsNodeId(std::string_view node_id) const
{
    return FindByNodeId(node_id) != nullptr;
}

bool ProcessRegistry::ContainsRoutingId(std::span<const std::byte> routing_id) const
{
    return FindByRoutingId(routing_id) != nullptr;
}

std::vector<InnerNetworkSession> ProcessRegistry::Snapshot() const
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

std::size_t ProcessRegistry::size() const noexcept
{
    return entries_by_node_id_.size();
}

void ProcessRegistry::Clear() noexcept
{
    entries_by_node_id_.clear();
    node_id_by_routing_key_.clear();
}

InnerNetworkSession* ProcessRegistry::FindMutableByNodeId(std::string_view node_id)
{
    auto iterator = entries_by_node_id_.find(node_id);
    if (iterator == entries_by_node_id_.end())
    {
        return nullptr;
    }

    return &iterator->second;
}

InnerNetworkSession* ProcessRegistry::FindMutableByRoutingId(std::span<const std::byte> routing_id)
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

#include "InnerClusterCodec.h"

#include "BinarySerialization.h"

#include <limits>

namespace xs::net
{
namespace
{

[[nodiscard]] InnerClusterCodecErrorCode MapSerializationError(SerializationErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case SerializationErrorCode::None:
        return InnerClusterCodecErrorCode::None;
    case SerializationErrorCode::BufferTooSmall:
        return InnerClusterCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::LengthOverflow:
        return InnerClusterCodecErrorCode::LengthOverflow;
    case SerializationErrorCode::InvalidBoolValue:
        return InnerClusterCodecErrorCode::InvalidBoolValue;
    case SerializationErrorCode::InvalidArgument:
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    return InnerClusterCodecErrorCode::InvalidArgument;
}

[[nodiscard]] InnerClusterCodecErrorCode AddWireSize(std::size_t delta, std::size_t* total) noexcept
{
    if (total == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    if (delta > std::numeric_limits<std::size_t>::max() - *total)
    {
        return InnerClusterCodecErrorCode::LengthOverflow;
    }

    *total += delta;
    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode GetString16WireSize(
    std::string_view value,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
    {
        return InnerClusterCodecErrorCode::LengthOverflow;
    }

    *wire_size = sizeof(std::uint16_t) + value.size();
    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode ValidateClusterReadyNotify(const ClusterReadyNotify& message) noexcept
{
    if (message.status_flags != 0u)
    {
        return InnerClusterCodecErrorCode::InvalidReadyStatusFlags;
    }

    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode ValidateClusterNodesOnlineNotify(const ClusterNodesOnlineNotify& message) noexcept
{
    if (message.status_flags != 0u)
    {
        return InnerClusterCodecErrorCode::InvalidNodesOnlineStatusFlags;
    }

    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode ValidateGameGateMeshReadyReport(
    const GameGateMeshReadyReport& message) noexcept
{
    if (message.status_flags != 0u)
    {
        return InnerClusterCodecErrorCode::InvalidMeshReadyStatusFlags;
    }

    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode ValidateServerStubOwnershipEntry(
    const ServerStubOwnershipEntry& entry) noexcept
{
    if (entry.entry_flags != 0u)
    {
        return InnerClusterCodecErrorCode::InvalidOwnershipEntryFlags;
    }

    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode ValidateServerStubOwnershipSync(
    const ServerStubOwnershipSync& message) noexcept
{
    if (message.status_flags != 0u)
    {
        return InnerClusterCodecErrorCode::InvalidOwnershipStatusFlags;
    }

    for (const ServerStubOwnershipEntry& entry : message.assignments)
    {
        const InnerClusterCodecErrorCode entry_result = ValidateServerStubOwnershipEntry(entry);
        if (entry_result != InnerClusterCodecErrorCode::None)
        {
            return entry_result;
        }
    }

    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode ValidateServerStubReadyEntry(
    const ServerStubReadyEntry& entry) noexcept
{
    if (entry.entry_flags != 0u)
    {
        return InnerClusterCodecErrorCode::InvalidServiceReadyEntryFlags;
    }

    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode ValidateGameServiceReadyReport(
    const GameServiceReadyReport& message) noexcept
{
    if (message.status_flags != 0u)
    {
        return InnerClusterCodecErrorCode::InvalidServiceReadyStatusFlags;
    }

    for (const ServerStubReadyEntry& entry : message.entries)
    {
        const InnerClusterCodecErrorCode entry_result = ValidateServerStubReadyEntry(entry);
        if (entry_result != InnerClusterCodecErrorCode::None)
        {
            return entry_result;
        }
    }

    return InnerClusterCodecErrorCode::None;
}

[[nodiscard]] InnerClusterCodecErrorCode CheckTrailingBytes(const BinaryReader& reader) noexcept
{
    if (reader.remaining() != 0u)
    {
        return InnerClusterCodecErrorCode::TrailingBytes;
    }

    return InnerClusterCodecErrorCode::None;
}

} // namespace

std::string_view InnerClusterCodecErrorMessage(InnerClusterCodecErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case InnerClusterCodecErrorCode::None:
        return "Success.";
    case InnerClusterCodecErrorCode::BufferTooSmall:
        return "Buffer does not contain enough bytes for the requested inner-cluster operation.";
    case InnerClusterCodecErrorCode::InvalidBoolValue:
        return "Inner-cluster bool field must be encoded as 0 or 1.";
    case InnerClusterCodecErrorCode::InvalidArgument:
        return "Inner-cluster codec output argument must not be null.";
    case InnerClusterCodecErrorCode::InvalidReadyStatusFlags:
        return "ClusterReadyNotify statusFlags must be zero.";
    case InnerClusterCodecErrorCode::TrailingBytes:
        return "Inner-cluster buffer must not contain trailing bytes.";
    case InnerClusterCodecErrorCode::InvalidNodesOnlineStatusFlags:
        return "ClusterNodesOnlineNotify statusFlags must be zero.";
    case InnerClusterCodecErrorCode::LengthOverflow:
        return "Inner-cluster message length exceeds the supported range.";
    case InnerClusterCodecErrorCode::InvalidMeshReadyStatusFlags:
        return "GameGateMeshReadyReport statusFlags must be zero.";
    case InnerClusterCodecErrorCode::InvalidOwnershipStatusFlags:
        return "ServerStubOwnershipSync statusFlags must be zero.";
    case InnerClusterCodecErrorCode::InvalidOwnershipEntryFlags:
        return "ServerStubOwnershipEntry entryFlags must be zero.";
    case InnerClusterCodecErrorCode::InvalidServiceReadyStatusFlags:
        return "GameServiceReadyReport statusFlags must be zero.";
    case InnerClusterCodecErrorCode::InvalidServiceReadyEntryFlags:
        return "ServerStubReadyEntry entryFlags must be zero.";
    }

    return "Unknown inner-cluster codec error.";
}

InnerClusterCodecErrorCode EncodeClusterNodesOnlineNotify(
    const ClusterNodesOnlineNotify& message,
    std::span<std::byte> buffer) noexcept
{
    const InnerClusterCodecErrorCode validation_result = ValidateClusterNodesOnlineNotify(message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteBool(message.all_nodes_online) ||
        !writer.WriteUInt32(message.status_flags) ||
        !writer.WriteUInt64(message.server_now_unix_ms))
    {
        return MapSerializationError(writer.error());
    }

    return InnerClusterCodecErrorCode::None;
}

InnerClusterCodecErrorCode DecodeClusterNodesOnlineNotify(
    std::span<const std::byte> buffer,
    ClusterNodesOnlineNotify* message) noexcept
{
    if (message == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    ClusterNodesOnlineNotify parsed_message{};
    if (!reader.ReadBool(&parsed_message.all_nodes_online) ||
        !reader.ReadUInt32(&parsed_message.status_flags) ||
        !reader.ReadUInt64(&parsed_message.server_now_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const InnerClusterCodecErrorCode validation_result = ValidateClusterNodesOnlineNotify(parsed_message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    const InnerClusterCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != InnerClusterCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return InnerClusterCodecErrorCode::None;
}

InnerClusterCodecErrorCode EncodeGameGateMeshReadyReport(
    const GameGateMeshReadyReport& message,
    std::span<std::byte> buffer) noexcept
{
    const InnerClusterCodecErrorCode validation_result = ValidateGameGateMeshReadyReport(message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteUInt32(message.status_flags) ||
        !writer.WriteUInt64(message.reported_at_unix_ms))
    {
        return MapSerializationError(writer.error());
    }

    return InnerClusterCodecErrorCode::None;
}

InnerClusterCodecErrorCode DecodeGameGateMeshReadyReport(
    std::span<const std::byte> buffer,
    GameGateMeshReadyReport* message) noexcept
{
    if (message == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    GameGateMeshReadyReport parsed_message{};
    if (!reader.ReadUInt32(&parsed_message.status_flags) ||
        !reader.ReadUInt64(&parsed_message.reported_at_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const InnerClusterCodecErrorCode validation_result = ValidateGameGateMeshReadyReport(parsed_message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    const InnerClusterCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != InnerClusterCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return InnerClusterCodecErrorCode::None;
}

InnerClusterCodecErrorCode GetServerStubOwnershipSyncWireSize(
    const ServerStubOwnershipSync& message,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    const InnerClusterCodecErrorCode validation_result = ValidateServerStubOwnershipSync(message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    *wire_size = 0u;

    InnerClusterCodecErrorCode add_result =
        AddWireSize(sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t), wire_size);
    if (add_result != InnerClusterCodecErrorCode::None)
    {
        return add_result;
    }

    for (const ServerStubOwnershipEntry& entry : message.assignments)
    {
        std::size_t entry_field_size = 0u;

        add_result = GetString16WireSize(entry.entity_type, &entry_field_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = AddWireSize(entry_field_size, wire_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = GetString16WireSize(entry.entity_id, &entry_field_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = AddWireSize(entry_field_size, wire_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = GetString16WireSize(entry.owner_game_node_id, &entry_field_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = AddWireSize(entry_field_size, wire_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = AddWireSize(sizeof(std::uint32_t), wire_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }
    }

    return AddWireSize(sizeof(std::uint64_t), wire_size);
}

InnerClusterCodecErrorCode EncodeServerStubOwnershipSync(
    const ServerStubOwnershipSync& message,
    std::span<std::byte> buffer) noexcept
{
    std::size_t wire_size = 0u;
    const InnerClusterCodecErrorCode wire_size_result =
        GetServerStubOwnershipSyncWireSize(message, &wire_size);
    if (wire_size_result != InnerClusterCodecErrorCode::None)
    {
        return wire_size_result;
    }

    if (buffer.size() < wire_size)
    {
        return InnerClusterCodecErrorCode::BufferTooSmall;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteUInt64(message.assignment_epoch) ||
        !writer.WriteUInt32(message.status_flags) ||
        !writer.WriteUInt32(static_cast<std::uint32_t>(message.assignments.size())))
    {
        return MapSerializationError(writer.error());
    }

    for (const ServerStubOwnershipEntry& entry : message.assignments)
    {
        if (!writer.WriteString16(entry.entity_type) ||
            !writer.WriteString16(entry.entity_id) ||
            !writer.WriteString16(entry.owner_game_node_id) ||
            !writer.WriteUInt32(entry.entry_flags))
        {
            return MapSerializationError(writer.error());
        }
    }

    if (!writer.WriteUInt64(message.server_now_unix_ms))
    {
        return MapSerializationError(writer.error());
    }

    return InnerClusterCodecErrorCode::None;
}

InnerClusterCodecErrorCode DecodeServerStubOwnershipSync(
    std::span<const std::byte> buffer,
    ServerStubOwnershipSync* message) noexcept
{
    if (message == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    std::uint32_t assignment_count = 0u;
    ServerStubOwnershipSync parsed_message{};
    if (!reader.ReadUInt64(&parsed_message.assignment_epoch) ||
        !reader.ReadUInt32(&parsed_message.status_flags) ||
        !reader.ReadUInt32(&assignment_count))
    {
        return MapSerializationError(reader.error());
    }

    parsed_message.assignments.reserve(static_cast<std::size_t>(assignment_count));
    for (std::uint32_t index = 0u; index < assignment_count; ++index)
    {
        ServerStubOwnershipEntry entry{};
        if (!reader.ReadString16(&entry.entity_type) ||
            !reader.ReadString16(&entry.entity_id) ||
            !reader.ReadString16(&entry.owner_game_node_id) ||
            !reader.ReadUInt32(&entry.entry_flags))
        {
            return MapSerializationError(reader.error());
        }

        const InnerClusterCodecErrorCode entry_result = ValidateServerStubOwnershipEntry(entry);
        if (entry_result != InnerClusterCodecErrorCode::None)
        {
            return entry_result;
        }

        parsed_message.assignments.push_back(std::move(entry));
    }

    if (!reader.ReadUInt64(&parsed_message.server_now_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const InnerClusterCodecErrorCode validation_result = ValidateServerStubOwnershipSync(parsed_message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    const InnerClusterCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != InnerClusterCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = std::move(parsed_message);
    return InnerClusterCodecErrorCode::None;
}

InnerClusterCodecErrorCode GetGameServiceReadyReportWireSize(
    const GameServiceReadyReport& message,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    const InnerClusterCodecErrorCode validation_result = ValidateGameServiceReadyReport(message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    *wire_size = 0u;

    InnerClusterCodecErrorCode add_result = AddWireSize(
        sizeof(std::uint64_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t),
        wire_size);
    if (add_result != InnerClusterCodecErrorCode::None)
    {
        return add_result;
    }

    for (const ServerStubReadyEntry& entry : message.entries)
    {
        std::size_t entry_field_size = 0u;

        add_result = GetString16WireSize(entry.entity_type, &entry_field_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = AddWireSize(entry_field_size, wire_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = GetString16WireSize(entry.entity_id, &entry_field_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = AddWireSize(entry_field_size, wire_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }

        add_result = AddWireSize(sizeof(std::uint8_t) + sizeof(std::uint32_t), wire_size);
        if (add_result != InnerClusterCodecErrorCode::None)
        {
            return add_result;
        }
    }

    return AddWireSize(sizeof(std::uint64_t), wire_size);
}

InnerClusterCodecErrorCode EncodeGameServiceReadyReport(
    const GameServiceReadyReport& message,
    std::span<std::byte> buffer) noexcept
{
    std::size_t wire_size = 0u;
    const InnerClusterCodecErrorCode wire_size_result =
        GetGameServiceReadyReportWireSize(message, &wire_size);
    if (wire_size_result != InnerClusterCodecErrorCode::None)
    {
        return wire_size_result;
    }

    if (buffer.size() < wire_size)
    {
        return InnerClusterCodecErrorCode::BufferTooSmall;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteUInt64(message.assignment_epoch) ||
        !writer.WriteBool(message.local_ready) ||
        !writer.WriteUInt32(message.status_flags) ||
        !writer.WriteUInt32(static_cast<std::uint32_t>(message.entries.size())))
    {
        return MapSerializationError(writer.error());
    }

    for (const ServerStubReadyEntry& entry : message.entries)
    {
        if (!writer.WriteString16(entry.entity_type) ||
            !writer.WriteString16(entry.entity_id) ||
            !writer.WriteBool(entry.ready) ||
            !writer.WriteUInt32(entry.entry_flags))
        {
            return MapSerializationError(writer.error());
        }
    }

    if (!writer.WriteUInt64(message.reported_at_unix_ms))
    {
        return MapSerializationError(writer.error());
    }

    return InnerClusterCodecErrorCode::None;
}

InnerClusterCodecErrorCode DecodeGameServiceReadyReport(
    std::span<const std::byte> buffer,
    GameServiceReadyReport* message) noexcept
{
    if (message == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    std::uint32_t entry_count = 0u;
    GameServiceReadyReport parsed_message{};
    if (!reader.ReadUInt64(&parsed_message.assignment_epoch) ||
        !reader.ReadBool(&parsed_message.local_ready) ||
        !reader.ReadUInt32(&parsed_message.status_flags) ||
        !reader.ReadUInt32(&entry_count))
    {
        return MapSerializationError(reader.error());
    }

    parsed_message.entries.reserve(static_cast<std::size_t>(entry_count));
    for (std::uint32_t index = 0u; index < entry_count; ++index)
    {
        ServerStubReadyEntry entry{};
        if (!reader.ReadString16(&entry.entity_type) ||
            !reader.ReadString16(&entry.entity_id) ||
            !reader.ReadBool(&entry.ready) ||
            !reader.ReadUInt32(&entry.entry_flags))
        {
            return MapSerializationError(reader.error());
        }

        const InnerClusterCodecErrorCode entry_result = ValidateServerStubReadyEntry(entry);
        if (entry_result != InnerClusterCodecErrorCode::None)
        {
            return entry_result;
        }

        parsed_message.entries.push_back(std::move(entry));
    }

    if (!reader.ReadUInt64(&parsed_message.reported_at_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const InnerClusterCodecErrorCode validation_result = ValidateGameServiceReadyReport(parsed_message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    const InnerClusterCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != InnerClusterCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = std::move(parsed_message);
    return InnerClusterCodecErrorCode::None;
}
InnerClusterCodecErrorCode EncodeClusterReadyNotify(
    const ClusterReadyNotify& message,
    std::span<std::byte> buffer) noexcept
{
    const InnerClusterCodecErrorCode validation_result = ValidateClusterReadyNotify(message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteUInt64(message.ready_epoch) ||
        !writer.WriteBool(message.cluster_ready) ||
        !writer.WriteUInt32(message.status_flags) ||
        !writer.WriteUInt64(message.server_now_unix_ms))
    {
        return MapSerializationError(writer.error());
    }

    return InnerClusterCodecErrorCode::None;
}

InnerClusterCodecErrorCode DecodeClusterReadyNotify(
    std::span<const std::byte> buffer,
    ClusterReadyNotify* message) noexcept
{
    if (message == nullptr)
    {
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    ClusterReadyNotify parsed_message{};
    if (!reader.ReadUInt64(&parsed_message.ready_epoch) ||
        !reader.ReadBool(&parsed_message.cluster_ready) ||
        !reader.ReadUInt32(&parsed_message.status_flags) ||
        !reader.ReadUInt64(&parsed_message.server_now_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const InnerClusterCodecErrorCode validation_result = ValidateClusterReadyNotify(parsed_message);
    if (validation_result != InnerClusterCodecErrorCode::None)
    {
        return validation_result;
    }

    const InnerClusterCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != InnerClusterCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return InnerClusterCodecErrorCode::None;
}

} // namespace xs::net

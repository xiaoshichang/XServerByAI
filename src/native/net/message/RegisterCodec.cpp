#include "RegisterCodec.h"

#include "BinarySerialization.h"

#include <limits>
#include <utility>

namespace xs::net
{
namespace
{

[[nodiscard]] RegisterCodecErrorCode MapSerializationError(SerializationErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case SerializationErrorCode::None:
        return RegisterCodecErrorCode::None;
    case SerializationErrorCode::BufferTooSmall:
        return RegisterCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::LengthOverflow:
        return RegisterCodecErrorCode::LengthOverflow;
    case SerializationErrorCode::InvalidArgument:
        return RegisterCodecErrorCode::InvalidArgument;
    case SerializationErrorCode::InvalidBoolValue:
        return RegisterCodecErrorCode::InvalidArgument;
    }

    return RegisterCodecErrorCode::InvalidArgument;
}

[[nodiscard]] RegisterCodecErrorCode AddWireSize(std::size_t delta, std::size_t* total) noexcept
{
    if (total == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    if (delta > std::numeric_limits<std::size_t>::max() - *total)
    {
        return RegisterCodecErrorCode::LengthOverflow;
    }

    *total += delta;
    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode GetString16WireSize(
    std::string_view value,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
    {
        return RegisterCodecErrorCode::LengthOverflow;
    }

    *wire_size = sizeof(std::uint16_t) + value.size();
    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode ValidateProcessType(std::uint16_t process_type) noexcept
{
    if (!IsValidControlProcessType(process_type))
    {
        return RegisterCodecErrorCode::InvalidProcessType;
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode ValidateProcessFlags(std::uint16_t process_flags) noexcept
{
    if (process_flags != 0u)
    {
        return RegisterCodecErrorCode::InvalidProcessFlags;
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode ValidateNodeId(std::string_view node_id) noexcept
{
    if (node_id.empty())
    {
        return RegisterCodecErrorCode::InvalidNodeId;
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode ValidateEndpoint(const Endpoint& endpoint) noexcept
{
    if (endpoint.host.empty())
    {
        return RegisterCodecErrorCode::InvalidServiceEndpointHost;
    }

    if (endpoint.port == 0u)
    {
        return RegisterCodecErrorCode::InvalidServiceEndpointPort;
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode ValidateCapabilityTagCount(std::size_t tag_count) noexcept
{
    if (tag_count > kRegisterMaxCapabilityTagCount)
    {
        return RegisterCodecErrorCode::TooManyCapabilityTags;
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode ValidateHeartbeatTiming(
    std::uint32_t heartbeat_interval_ms,
    std::uint32_t heartbeat_timeout_ms) noexcept
{
    if (heartbeat_interval_ms >= heartbeat_timeout_ms)
    {
        return RegisterCodecErrorCode::InvalidHeartbeatTiming;
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode ValidateRegisterRequest(const RegisterRequest& message) noexcept
{
    const RegisterCodecErrorCode process_type_result = ValidateProcessType(message.process_type);
    if (process_type_result != RegisterCodecErrorCode::None)
    {
        return process_type_result;
    }

    const RegisterCodecErrorCode process_flags_result = ValidateProcessFlags(message.process_flags);
    if (process_flags_result != RegisterCodecErrorCode::None)
    {
        return process_flags_result;
    }

    const RegisterCodecErrorCode node_id_result = ValidateNodeId(message.node_id);
    if (node_id_result != RegisterCodecErrorCode::None)
    {
        return node_id_result;
    }

    const RegisterCodecErrorCode endpoint_result = ValidateEndpoint(message.service_endpoint);
    if (endpoint_result != RegisterCodecErrorCode::None)
    {
        return endpoint_result;
    }

    return ValidateCapabilityTagCount(message.capability_tags.size());
}

[[nodiscard]] RegisterCodecErrorCode ValidateRegisterSuccessResponse(const RegisterSuccessResponse& message) noexcept
{
    return ValidateHeartbeatTiming(message.heartbeat_interval_ms, message.heartbeat_timeout_ms);
}

[[nodiscard]] RegisterCodecErrorCode GetEndpointWireSize(
    const Endpoint& endpoint,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    const RegisterCodecErrorCode endpoint_result = ValidateEndpoint(endpoint);
    if (endpoint_result != RegisterCodecErrorCode::None)
    {
        return endpoint_result;
    }

    std::size_t total = 0;
    std::size_t host_wire_size = 0;
    const RegisterCodecErrorCode host_size_result = GetString16WireSize(endpoint.host, &host_wire_size);
    if (host_size_result != RegisterCodecErrorCode::None)
    {
        return host_size_result;
    }

    const RegisterCodecErrorCode add_host_result = AddWireSize(host_wire_size, &total);
    if (add_host_result != RegisterCodecErrorCode::None)
    {
        return add_host_result;
    }

    const RegisterCodecErrorCode add_port_result = AddWireSize(sizeof(std::uint16_t), &total);
    if (add_port_result != RegisterCodecErrorCode::None)
    {
        return add_port_result;
    }

    *wire_size = total;
    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode GetCapabilityTagsWireSize(
    const std::vector<std::string>& capability_tags,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    const RegisterCodecErrorCode count_result = ValidateCapabilityTagCount(capability_tags.size());
    if (count_result != RegisterCodecErrorCode::None)
    {
        return count_result;
    }

    std::size_t total = sizeof(std::uint16_t);
    for (const std::string& capability_tag : capability_tags)
    {
        std::size_t tag_wire_size = 0;
        const RegisterCodecErrorCode tag_size_result = GetString16WireSize(capability_tag, &tag_wire_size);
        if (tag_size_result != RegisterCodecErrorCode::None)
        {
            return tag_size_result;
        }

        const RegisterCodecErrorCode add_tag_result = AddWireSize(tag_wire_size, &total);
        if (add_tag_result != RegisterCodecErrorCode::None)
        {
            return add_tag_result;
        }
    }

    *wire_size = total;
    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode EncodeEndpoint(
    const Endpoint& endpoint,
    BinaryWriter* writer) noexcept
{
    if (writer == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    const RegisterCodecErrorCode endpoint_result = ValidateEndpoint(endpoint);
    if (endpoint_result != RegisterCodecErrorCode::None)
    {
        return endpoint_result;
    }

    if (!writer->WriteString16(endpoint.host) ||
        !writer->WriteUInt16(endpoint.port))
    {
        return MapSerializationError(writer->error());
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode DecodeEndpoint(
    BinaryReader* reader,
    Endpoint* endpoint) noexcept
{
    if (reader == nullptr || endpoint == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    Endpoint parsed_endpoint{};
    if (!reader->ReadString16(&parsed_endpoint.host) ||
        !reader->ReadUInt16(&parsed_endpoint.port))
    {
        return MapSerializationError(reader->error());
    }

    *endpoint = std::move(parsed_endpoint);
    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode EncodeLoadSnapshot(
    const LoadSnapshot& snapshot,
    BinaryWriter* writer) noexcept
{
    if (writer == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    if (!writer->WriteUInt32(snapshot.connection_count) ||
        !writer->WriteUInt32(snapshot.session_count) ||
        !writer->WriteUInt32(snapshot.entity_count) ||
        !writer->WriteUInt32(snapshot.space_count) ||
        !writer->WriteUInt32(snapshot.load_score))
    {
        return MapSerializationError(writer->error());
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode DecodeLoadSnapshot(
    BinaryReader* reader,
    LoadSnapshot* snapshot) noexcept
{
    if (reader == nullptr || snapshot == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    LoadSnapshot parsed_snapshot{};
    if (!reader->ReadUInt32(&parsed_snapshot.connection_count) ||
        !reader->ReadUInt32(&parsed_snapshot.session_count) ||
        !reader->ReadUInt32(&parsed_snapshot.entity_count) ||
        !reader->ReadUInt32(&parsed_snapshot.space_count) ||
        !reader->ReadUInt32(&parsed_snapshot.load_score))
    {
        return MapSerializationError(reader->error());
    }

    *snapshot = parsed_snapshot;
    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode EncodeCapabilityTags(
    const std::vector<std::string>& capability_tags,
    BinaryWriter* writer) noexcept
{
    if (writer == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    const RegisterCodecErrorCode count_result = ValidateCapabilityTagCount(capability_tags.size());
    if (count_result != RegisterCodecErrorCode::None)
    {
        return count_result;
    }

    if (!writer->WriteUInt16(static_cast<std::uint16_t>(capability_tags.size())))
    {
        return MapSerializationError(writer->error());
    }

    for (const std::string& capability_tag : capability_tags)
    {
        if (!writer->WriteString16(capability_tag))
        {
            return MapSerializationError(writer->error());
        }
    }

    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode DecodeCapabilityTags(
    BinaryReader* reader,
    std::vector<std::string>* capability_tags) noexcept
{
    if (reader == nullptr || capability_tags == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    std::uint16_t tag_count = 0;
    if (!reader->ReadUInt16(&tag_count))
    {
        return MapSerializationError(reader->error());
    }

    const RegisterCodecErrorCode count_result = ValidateCapabilityTagCount(tag_count);
    if (count_result != RegisterCodecErrorCode::None)
    {
        return count_result;
    }

    std::vector<std::string> parsed_capability_tags;
    parsed_capability_tags.reserve(tag_count);
    for (std::uint16_t index = 0; index < tag_count; ++index)
    {
        std::string capability_tag;
        if (!reader->ReadString16(&capability_tag))
        {
            return MapSerializationError(reader->error());
        }

        parsed_capability_tags.push_back(std::move(capability_tag));
    }

    *capability_tags = std::move(parsed_capability_tags);
    return RegisterCodecErrorCode::None;
}

[[nodiscard]] RegisterCodecErrorCode CheckTrailingBytes(const BinaryReader& reader) noexcept
{
    if (reader.remaining() != 0u)
    {
        return RegisterCodecErrorCode::TrailingBytes;
    }

    return RegisterCodecErrorCode::None;
}

} // namespace

std::string_view RegisterCodecErrorMessage(RegisterCodecErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case RegisterCodecErrorCode::None:
        return "Success.";
    case RegisterCodecErrorCode::BufferTooSmall:
        return "Buffer does not contain enough bytes for the requested register operation.";
    case RegisterCodecErrorCode::LengthOverflow:
        return "Register field length exceeds the supported prefix range.";
    case RegisterCodecErrorCode::InvalidArgument:
        return "Register codec output argument must not be null.";
    case RegisterCodecErrorCode::InvalidProcessType:
        return "Register processType must be Gate or Game.";
    case RegisterCodecErrorCode::InvalidProcessFlags:
        return "Register processFlags must be zero.";
    case RegisterCodecErrorCode::InvalidNodeId:
        return "Register nodeId must not be empty.";
    case RegisterCodecErrorCode::InvalidServiceEndpointHost:
        return "Register serviceEndpoint.host must not be empty.";
    case RegisterCodecErrorCode::InvalidServiceEndpointPort:
        return "Register serviceEndpoint.port must not be zero.";
    case RegisterCodecErrorCode::InvalidHeartbeatTiming:
        return "Register heartbeat interval must be less than timeout.";
    case RegisterCodecErrorCode::TooManyCapabilityTags:
        return "Register capabilityTags count must not exceed 32.";
    case RegisterCodecErrorCode::TrailingBytes:
        return "Register buffer must not contain trailing bytes.";
    }

    return "Unknown register codec error.";
}

RegisterCodecErrorCode GetRegisterRequestWireSize(
    const RegisterRequest& message,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    *wire_size = 0;

    const RegisterCodecErrorCode validation_result = ValidateRegisterRequest(message);
    if (validation_result != RegisterCodecErrorCode::None)
    {
        return validation_result;
    }

    const RegisterCodecErrorCode add_fixed_header_result =
        AddWireSize(sizeof(std::uint16_t) + sizeof(std::uint16_t), wire_size);
    if (add_fixed_header_result != RegisterCodecErrorCode::None)
    {
        return add_fixed_header_result;
    }

    std::size_t field_wire_size = 0;
    const RegisterCodecErrorCode node_id_size_result = GetString16WireSize(message.node_id, &field_wire_size);
    if (node_id_size_result != RegisterCodecErrorCode::None)
    {
        return node_id_size_result;
    }

    const RegisterCodecErrorCode add_node_id_result = AddWireSize(field_wire_size, wire_size);
    if (add_node_id_result != RegisterCodecErrorCode::None)
    {
        return add_node_id_result;
    }

    const RegisterCodecErrorCode add_pid_result = AddWireSize(sizeof(std::uint32_t), wire_size);
    if (add_pid_result != RegisterCodecErrorCode::None)
    {
        return add_pid_result;
    }

    const RegisterCodecErrorCode add_started_at_result = AddWireSize(sizeof(std::uint64_t), wire_size);
    if (add_started_at_result != RegisterCodecErrorCode::None)
    {
        return add_started_at_result;
    }

    const RegisterCodecErrorCode endpoint_size_result = GetEndpointWireSize(message.service_endpoint, &field_wire_size);
    if (endpoint_size_result != RegisterCodecErrorCode::None)
    {
        return endpoint_size_result;
    }

    const RegisterCodecErrorCode add_endpoint_result = AddWireSize(field_wire_size, wire_size);
    if (add_endpoint_result != RegisterCodecErrorCode::None)
    {
        return add_endpoint_result;
    }

    const RegisterCodecErrorCode build_version_size_result =
        GetString16WireSize(message.build_version, &field_wire_size);
    if (build_version_size_result != RegisterCodecErrorCode::None)
    {
        return build_version_size_result;
    }

    const RegisterCodecErrorCode add_build_version_result = AddWireSize(field_wire_size, wire_size);
    if (add_build_version_result != RegisterCodecErrorCode::None)
    {
        return add_build_version_result;
    }

    const RegisterCodecErrorCode capability_tags_size_result =
        GetCapabilityTagsWireSize(message.capability_tags, &field_wire_size);
    if (capability_tags_size_result != RegisterCodecErrorCode::None)
    {
        return capability_tags_size_result;
    }

    const RegisterCodecErrorCode add_capability_tags_result = AddWireSize(field_wire_size, wire_size);
    if (add_capability_tags_result != RegisterCodecErrorCode::None)
    {
        return add_capability_tags_result;
    }

    return AddWireSize(kControlLoadSnapshotSize, wire_size);
}

RegisterCodecErrorCode EncodeRegisterRequest(
    const RegisterRequest& message,
    std::span<std::byte> buffer) noexcept
{
    std::size_t wire_size = 0;
    const RegisterCodecErrorCode wire_size_result = GetRegisterRequestWireSize(message, &wire_size);
    if (wire_size_result != RegisterCodecErrorCode::None)
    {
        return wire_size_result;
    }

    if (buffer.size() < wire_size)
    {
        return RegisterCodecErrorCode::BufferTooSmall;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteUInt16(message.process_type) ||
        !writer.WriteUInt16(message.process_flags) ||
        !writer.WriteString16(message.node_id) ||
        !writer.WriteUInt32(message.pid) ||
        !writer.WriteUInt64(message.started_at_unix_ms))
    {
        return MapSerializationError(writer.error());
    }

    const RegisterCodecErrorCode endpoint_result = EncodeEndpoint(message.service_endpoint, &writer);
    if (endpoint_result != RegisterCodecErrorCode::None)
    {
        return endpoint_result;
    }

    if (!writer.WriteString16(message.build_version))
    {
        return MapSerializationError(writer.error());
    }

    const RegisterCodecErrorCode capability_tags_result = EncodeCapabilityTags(message.capability_tags, &writer);
    if (capability_tags_result != RegisterCodecErrorCode::None)
    {
        return capability_tags_result;
    }

    return EncodeLoadSnapshot(message.load, &writer);
}

RegisterCodecErrorCode DecodeRegisterRequest(
    std::span<const std::byte> buffer,
    RegisterRequest* message) noexcept
{
    if (message == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    RegisterRequest parsed_message{};
    if (!reader.ReadUInt16(&parsed_message.process_type) ||
        !reader.ReadUInt16(&parsed_message.process_flags) ||
        !reader.ReadString16(&parsed_message.node_id) ||
        !reader.ReadUInt32(&parsed_message.pid) ||
        !reader.ReadUInt64(&parsed_message.started_at_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const RegisterCodecErrorCode endpoint_result = DecodeEndpoint(&reader, &parsed_message.service_endpoint);
    if (endpoint_result != RegisterCodecErrorCode::None)
    {
        return endpoint_result;
    }

    if (!reader.ReadString16(&parsed_message.build_version))
    {
        return MapSerializationError(reader.error());
    }

    const RegisterCodecErrorCode capability_tags_result =
        DecodeCapabilityTags(&reader, &parsed_message.capability_tags);
    if (capability_tags_result != RegisterCodecErrorCode::None)
    {
        return capability_tags_result;
    }

    const RegisterCodecErrorCode load_result = DecodeLoadSnapshot(&reader, &parsed_message.load);
    if (load_result != RegisterCodecErrorCode::None)
    {
        return load_result;
    }

    const RegisterCodecErrorCode validation_result = ValidateRegisterRequest(parsed_message);
    if (validation_result != RegisterCodecErrorCode::None)
    {
        return validation_result;
    }

    const RegisterCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != RegisterCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = std::move(parsed_message);
    return RegisterCodecErrorCode::None;
}

RegisterCodecErrorCode EncodeRegisterSuccessResponse(
    const RegisterSuccessResponse& message,
    std::span<std::byte> buffer) noexcept
{
    const RegisterCodecErrorCode validation_result = ValidateRegisterSuccessResponse(message);
    if (validation_result != RegisterCodecErrorCode::None)
    {
        return validation_result;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteUInt32(message.heartbeat_interval_ms) ||
        !writer.WriteUInt32(message.heartbeat_timeout_ms) ||
        !writer.WriteUInt64(message.server_now_unix_ms))
    {
        return MapSerializationError(writer.error());
    }

    return RegisterCodecErrorCode::None;
}

RegisterCodecErrorCode DecodeRegisterSuccessResponse(
    std::span<const std::byte> buffer,
    RegisterSuccessResponse* message) noexcept
{
    if (message == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    RegisterSuccessResponse parsed_message{};
    if (!reader.ReadUInt32(&parsed_message.heartbeat_interval_ms) ||
        !reader.ReadUInt32(&parsed_message.heartbeat_timeout_ms) ||
        !reader.ReadUInt64(&parsed_message.server_now_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const RegisterCodecErrorCode validation_result = ValidateRegisterSuccessResponse(parsed_message);
    if (validation_result != RegisterCodecErrorCode::None)
    {
        return validation_result;
    }

    const RegisterCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != RegisterCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return RegisterCodecErrorCode::None;
}

RegisterCodecErrorCode EncodeRegisterErrorResponse(
    const RegisterErrorResponse& message,
    std::span<std::byte> buffer) noexcept
{
    BinaryWriter writer(buffer);
    if (!writer.WriteInt32(message.error_code) ||
        !writer.WriteUInt32(message.retry_after_ms))
    {
        return MapSerializationError(writer.error());
    }

    return RegisterCodecErrorCode::None;
}

RegisterCodecErrorCode DecodeRegisterErrorResponse(
    std::span<const std::byte> buffer,
    RegisterErrorResponse* message) noexcept
{
    if (message == nullptr)
    {
        return RegisterCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    RegisterErrorResponse parsed_message{};
    if (!reader.ReadInt32(&parsed_message.error_code) ||
        !reader.ReadUInt32(&parsed_message.retry_after_ms))
    {
        return MapSerializationError(reader.error());
    }

    const RegisterCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != RegisterCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return RegisterCodecErrorCode::None;
}

} // namespace xs::net


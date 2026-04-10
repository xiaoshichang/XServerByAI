#include "RelayCodec.h"

#include "BinarySerialization.h"

#include <cctype>
#include <limits>
#include <utility>

namespace xs::net
{
namespace
{

[[nodiscard]] RelayCodecErrorCode MapSerializationError(SerializationErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case SerializationErrorCode::None:
        return RelayCodecErrorCode::None;
    case SerializationErrorCode::BufferTooSmall:
        return RelayCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::LengthOverflow:
        return RelayCodecErrorCode::LengthOverflow;
    case SerializationErrorCode::InvalidArgument:
    case SerializationErrorCode::InvalidBoolValue:
        return RelayCodecErrorCode::InvalidArgument;
    }

    return RelayCodecErrorCode::InvalidArgument;
}

[[nodiscard]] RelayCodecErrorCode AddWireSize(std::size_t delta, std::size_t* total) noexcept
{
    if (total == nullptr)
    {
        return RelayCodecErrorCode::InvalidArgument;
    }

    if (delta > std::numeric_limits<std::size_t>::max() - *total)
    {
        return RelayCodecErrorCode::LengthOverflow;
    }

    *total += delta;
    return RelayCodecErrorCode::None;
}

[[nodiscard]] RelayCodecErrorCode GetString16WireSize(
    std::string_view value,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return RelayCodecErrorCode::InvalidArgument;
    }

    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()))
    {
        return RelayCodecErrorCode::LengthOverflow;
    }

    *wire_size = sizeof(std::uint16_t) + value.size();
    return RelayCodecErrorCode::None;
}

[[nodiscard]] bool IsCanonicalGuidText(std::string_view value) noexcept
{
    if (value.size() != 36u)
    {
        return false;
    }

    for (std::size_t index = 0u; index < value.size(); ++index)
    {
        const char ch = value[index];
        const bool should_be_dash =
            index == 8u || index == 13u || index == 18u || index == 23u;
        if (should_be_dash)
        {
            if (ch != '-')
            {
                return false;
            }

            continue;
        }

        if (std::isxdigit(static_cast<unsigned char>(ch)) == 0)
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] RelayCodecErrorCode ValidateRelayForwardStubCall(
    const RelayForwardStubCall& message) noexcept
{
    if (message.source_game_node_id.empty())
    {
        return RelayCodecErrorCode::InvalidSourceGameNodeId;
    }

    if (message.target_game_node_id.empty())
    {
        return RelayCodecErrorCode::InvalidTargetGameNodeId;
    }

    if (message.target_stub_type.empty())
    {
        return RelayCodecErrorCode::InvalidTargetStubType;
    }

    if (message.stub_call_msg_id == 0u)
    {
        return RelayCodecErrorCode::InvalidMessageId;
    }

    if (message.relay_flags != 0u)
    {
        return RelayCodecErrorCode::InvalidRelayFlags;
    }

    return RelayCodecErrorCode::None;
}

[[nodiscard]] RelayCodecErrorCode ValidateRelayForwardProxyCall(
    const RelayForwardProxyCall& message) noexcept
{
    if (message.source_game_node_id.empty())
    {
        return RelayCodecErrorCode::InvalidSourceGameNodeId;
    }

    if (message.route_gate_node_id.empty())
    {
        return RelayCodecErrorCode::InvalidRouteGateNodeId;
    }

    if (!IsCanonicalGuidText(message.target_entity_id))
    {
        return RelayCodecErrorCode::InvalidTargetEntityId;
    }

    if (message.proxy_call_msg_id == 0u)
    {
        return RelayCodecErrorCode::InvalidMessageId;
    }

    if (message.relay_flags != 0u)
    {
        return RelayCodecErrorCode::InvalidRelayFlags;
    }

    return RelayCodecErrorCode::None;
}

[[nodiscard]] RelayCodecErrorCode CheckTrailingBytes(const BinaryReader& reader) noexcept
{
    if (reader.remaining() != 0u)
    {
        return RelayCodecErrorCode::TrailingBytes;
    }

    return RelayCodecErrorCode::None;
}

} // namespace

std::string_view RelayCodecErrorMessage(RelayCodecErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case RelayCodecErrorCode::None:
        return "Success.";
    case RelayCodecErrorCode::BufferTooSmall:
        return "Buffer does not contain enough bytes for the requested relay operation.";
    case RelayCodecErrorCode::LengthOverflow:
        return "Relay message length exceeds the supported range.";
    case RelayCodecErrorCode::InvalidArgument:
        return "Relay codec output argument must not be null.";
    case RelayCodecErrorCode::InvalidSourceGameNodeId:
        return "Relay sourceGameNodeId must not be empty.";
    case RelayCodecErrorCode::InvalidTargetGameNodeId:
        return "Relay targetGameNodeId must not be empty.";
    case RelayCodecErrorCode::InvalidTargetStubType:
        return "Relay targetStubType must not be empty.";
    case RelayCodecErrorCode::InvalidRouteGateNodeId:
        return "Relay routeGateNodeId must not be empty.";
    case RelayCodecErrorCode::InvalidTargetEntityId:
        return "Relay targetEntityId must be a canonical GUID string.";
    case RelayCodecErrorCode::InvalidMessageId:
        return "Relay message msgId must not be zero.";
    case RelayCodecErrorCode::InvalidRelayFlags:
        return "Relay relayFlags must be zero.";
    case RelayCodecErrorCode::TrailingBytes:
        return "Relay buffer must not contain trailing bytes.";
    }

    return "Unknown relay codec error.";
}

RelayCodecErrorCode GetRelayForwardStubCallWireSize(
    const RelayForwardStubCall& message,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return RelayCodecErrorCode::InvalidArgument;
    }

    *wire_size = 0u;
    const RelayCodecErrorCode validation_result = ValidateRelayForwardStubCall(message);
    if (validation_result != RelayCodecErrorCode::None)
    {
        return validation_result;
    }

    std::size_t field_size = 0u;
    RelayCodecErrorCode result = GetString16WireSize(message.source_game_node_id, &field_size);
    if (result != RelayCodecErrorCode::None ||
        (result = AddWireSize(field_size, wire_size)) != RelayCodecErrorCode::None)
    {
        return result;
    }

    result = GetString16WireSize(message.target_game_node_id, &field_size);
    if (result != RelayCodecErrorCode::None ||
        (result = AddWireSize(field_size, wire_size)) != RelayCodecErrorCode::None)
    {
        return result;
    }

    result = GetString16WireSize(message.target_stub_type, &field_size);
    if (result != RelayCodecErrorCode::None ||
        (result = AddWireSize(field_size, wire_size)) != RelayCodecErrorCode::None)
    {
        return result;
    }

    if (message.payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return RelayCodecErrorCode::LengthOverflow;
    }

    result = AddWireSize(sizeof(std::uint32_t) * 3u + message.payload.size(), wire_size);
    if (result != RelayCodecErrorCode::None)
    {
        return result;
    }

    return RelayCodecErrorCode::None;
}

RelayCodecErrorCode EncodeRelayForwardStubCall(
    const RelayForwardStubCall& message,
    std::span<std::byte> buffer) noexcept
{
    const RelayCodecErrorCode validation_result = ValidateRelayForwardStubCall(message);
    if (validation_result != RelayCodecErrorCode::None)
    {
        return validation_result;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteString16(message.source_game_node_id) ||
        !writer.WriteString16(message.target_game_node_id) ||
        !writer.WriteString16(message.target_stub_type) ||
        !writer.WriteUInt32(message.stub_call_msg_id) ||
        !writer.WriteUInt32(message.relay_flags) ||
        !writer.WriteLengthPrefixedBytes32(message.payload))
    {
        return MapSerializationError(writer.error());
    }

    return RelayCodecErrorCode::None;
}

RelayCodecErrorCode DecodeRelayForwardStubCall(
    std::span<const std::byte> buffer,
    RelayForwardStubCall* message) noexcept
{
    if (message == nullptr)
    {
        return RelayCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    RelayForwardStubCall parsed_message{};
    std::span<const std::byte> payload{};
    if (!reader.ReadString16(&parsed_message.source_game_node_id) ||
        !reader.ReadString16(&parsed_message.target_game_node_id) ||
        !reader.ReadString16(&parsed_message.target_stub_type) ||
        !reader.ReadUInt32(&parsed_message.stub_call_msg_id) ||
        !reader.ReadUInt32(&parsed_message.relay_flags) ||
        !reader.ReadLengthPrefixedBytes32(&payload))
    {
        return MapSerializationError(reader.error());
    }

    const RelayCodecErrorCode validation_result = ValidateRelayForwardStubCall(parsed_message);
    if (validation_result != RelayCodecErrorCode::None)
    {
        return validation_result;
    }

    const RelayCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != RelayCodecErrorCode::None)
    {
        return trailing_result;
    }

    parsed_message.payload.assign(payload.begin(), payload.end());
    *message = std::move(parsed_message);
    return RelayCodecErrorCode::None;
}

RelayCodecErrorCode GetRelayForwardProxyCallWireSize(
    const RelayForwardProxyCall& message,
    std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return RelayCodecErrorCode::InvalidArgument;
    }

    *wire_size = 0u;
    const RelayCodecErrorCode validation_result = ValidateRelayForwardProxyCall(message);
    if (validation_result != RelayCodecErrorCode::None)
    {
        return validation_result;
    }

    std::size_t field_size = 0u;
    RelayCodecErrorCode result = GetString16WireSize(message.source_game_node_id, &field_size);
    if (result != RelayCodecErrorCode::None ||
        (result = AddWireSize(field_size, wire_size)) != RelayCodecErrorCode::None)
    {
        return result;
    }

    result = GetString16WireSize(message.route_gate_node_id, &field_size);
    if (result != RelayCodecErrorCode::None ||
        (result = AddWireSize(field_size, wire_size)) != RelayCodecErrorCode::None)
    {
        return result;
    }

    result = GetString16WireSize(message.target_entity_id, &field_size);
    if (result != RelayCodecErrorCode::None ||
        (result = AddWireSize(field_size, wire_size)) != RelayCodecErrorCode::None)
    {
        return result;
    }

    if (message.payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return RelayCodecErrorCode::LengthOverflow;
    }

    result = AddWireSize(sizeof(std::uint32_t) * 3u + message.payload.size(), wire_size);
    if (result != RelayCodecErrorCode::None)
    {
        return result;
    }

    return RelayCodecErrorCode::None;
}

RelayCodecErrorCode EncodeRelayForwardProxyCall(
    const RelayForwardProxyCall& message,
    std::span<std::byte> buffer) noexcept
{
    const RelayCodecErrorCode validation_result = ValidateRelayForwardProxyCall(message);
    if (validation_result != RelayCodecErrorCode::None)
    {
        return validation_result;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteString16(message.source_game_node_id) ||
        !writer.WriteString16(message.route_gate_node_id) ||
        !writer.WriteString16(message.target_entity_id) ||
        !writer.WriteUInt32(message.proxy_call_msg_id) ||
        !writer.WriteUInt32(message.relay_flags) ||
        !writer.WriteLengthPrefixedBytes32(message.payload))
    {
        return MapSerializationError(writer.error());
    }

    return RelayCodecErrorCode::None;
}

RelayCodecErrorCode DecodeRelayForwardProxyCall(
    std::span<const std::byte> buffer,
    RelayForwardProxyCall* message) noexcept
{
    if (message == nullptr)
    {
        return RelayCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    RelayForwardProxyCall parsed_message{};
    std::span<const std::byte> payload{};
    if (!reader.ReadString16(&parsed_message.source_game_node_id) ||
        !reader.ReadString16(&parsed_message.route_gate_node_id) ||
        !reader.ReadString16(&parsed_message.target_entity_id) ||
        !reader.ReadUInt32(&parsed_message.proxy_call_msg_id) ||
        !reader.ReadUInt32(&parsed_message.relay_flags) ||
        !reader.ReadLengthPrefixedBytes32(&payload))
    {
        return MapSerializationError(reader.error());
    }

    const RelayCodecErrorCode validation_result = ValidateRelayForwardProxyCall(parsed_message);
    if (validation_result != RelayCodecErrorCode::None)
    {
        return validation_result;
    }

    const RelayCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != RelayCodecErrorCode::None)
    {
        return trailing_result;
    }

    parsed_message.payload.assign(payload.begin(), payload.end());
    *message = std::move(parsed_message);
    return RelayCodecErrorCode::None;
}

} // namespace xs::net

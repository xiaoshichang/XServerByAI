#include "HeartbeatCodec.h"

#include "BinarySerialization.h"

namespace xs::net
{
namespace
{

[[nodiscard]] HeartbeatCodecErrorCode MapSerializationError(SerializationErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case SerializationErrorCode::None:
        return HeartbeatCodecErrorCode::None;
    case SerializationErrorCode::BufferTooSmall:
        return HeartbeatCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::LengthOverflow:
        return HeartbeatCodecErrorCode::LengthOverflow;
    case SerializationErrorCode::InvalidBoolValue:
        return HeartbeatCodecErrorCode::InvalidBoolValue;
    case SerializationErrorCode::InvalidArgument:
        return HeartbeatCodecErrorCode::InvalidArgument;
    }

    return HeartbeatCodecErrorCode::InvalidArgument;
}

[[nodiscard]] HeartbeatCodecErrorCode ValidateRegistrationId(std::uint64_t registration_id) noexcept
{
    if (registration_id == 0u)
    {
        return HeartbeatCodecErrorCode::InvalidRegistrationId;
    }

    return HeartbeatCodecErrorCode::None;
}

[[nodiscard]] HeartbeatCodecErrorCode ValidateStatusFlags(std::uint32_t status_flags) noexcept
{
    if (status_flags != 0u)
    {
        return HeartbeatCodecErrorCode::InvalidStatusFlags;
    }

    return HeartbeatCodecErrorCode::None;
}

[[nodiscard]] HeartbeatCodecErrorCode ValidateHeartbeatTiming(
    std::uint32_t heartbeat_interval_ms,
    std::uint32_t heartbeat_timeout_ms) noexcept
{
    if (heartbeat_interval_ms >= heartbeat_timeout_ms)
    {
        return HeartbeatCodecErrorCode::InvalidHeartbeatTiming;
    }

    return HeartbeatCodecErrorCode::None;
}

[[nodiscard]] HeartbeatCodecErrorCode EncodeLoadSnapshot(
    const LoadSnapshot& snapshot,
    BinaryWriter* writer) noexcept
{
    if (writer == nullptr)
    {
        return HeartbeatCodecErrorCode::InvalidArgument;
    }

    if (!writer->WriteUInt32(snapshot.connection_count) ||
        !writer->WriteUInt32(snapshot.session_count) ||
        !writer->WriteUInt32(snapshot.entity_count) ||
        !writer->WriteUInt32(snapshot.space_count) ||
        !writer->WriteUInt32(snapshot.load_score))
    {
        return MapSerializationError(writer->error());
    }

    return HeartbeatCodecErrorCode::None;
}

[[nodiscard]] HeartbeatCodecErrorCode DecodeLoadSnapshot(
    BinaryReader* reader,
    LoadSnapshot* snapshot) noexcept
{
    if (reader == nullptr || snapshot == nullptr)
    {
        return HeartbeatCodecErrorCode::InvalidArgument;
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
    return HeartbeatCodecErrorCode::None;
}

[[nodiscard]] HeartbeatCodecErrorCode CheckTrailingBytes(const BinaryReader& reader) noexcept
{
    if (reader.remaining() != 0u)
    {
        return HeartbeatCodecErrorCode::TrailingBytes;
    }

    return HeartbeatCodecErrorCode::None;
}

} // namespace

std::string_view HeartbeatCodecErrorMessage(HeartbeatCodecErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case HeartbeatCodecErrorCode::None:
        return "Success.";
    case HeartbeatCodecErrorCode::BufferTooSmall:
        return "Buffer does not contain enough bytes for the requested heartbeat operation.";
    case HeartbeatCodecErrorCode::LengthOverflow:
        return "Heartbeat field length exceeds the supported prefix range.";
    case HeartbeatCodecErrorCode::InvalidBoolValue:
        return "Heartbeat bool field must be encoded as 0 or 1.";
    case HeartbeatCodecErrorCode::InvalidArgument:
        return "Heartbeat codec output argument must not be null.";
    case HeartbeatCodecErrorCode::InvalidRegistrationId:
        return "Heartbeat registrationId must not be zero.";
    case HeartbeatCodecErrorCode::InvalidStatusFlags:
        return "Heartbeat statusFlags must be zero.";
    case HeartbeatCodecErrorCode::InvalidHeartbeatTiming:
        return "Heartbeat interval must be less than timeout.";
    case HeartbeatCodecErrorCode::TrailingBytes:
        return "Heartbeat buffer must not contain trailing bytes.";
    }

    return "Unknown heartbeat codec error.";
}

HeartbeatCodecErrorCode EncodeHeartbeatRequest(
    const HeartbeatRequest& message,
    std::span<std::byte> buffer) noexcept
{
    const HeartbeatCodecErrorCode registration_result = ValidateRegistrationId(message.registration_id);
    if (registration_result != HeartbeatCodecErrorCode::None)
    {
        return registration_result;
    }

    const HeartbeatCodecErrorCode status_result = ValidateStatusFlags(message.status_flags);
    if (status_result != HeartbeatCodecErrorCode::None)
    {
        return status_result;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteUInt64(message.registration_id) ||
        !writer.WriteUInt64(message.sent_at_unix_ms) ||
        !writer.WriteUInt32(message.status_flags))
    {
        return MapSerializationError(writer.error());
    }

    return EncodeLoadSnapshot(message.load, &writer);
}

HeartbeatCodecErrorCode DecodeHeartbeatRequest(
    std::span<const std::byte> buffer,
    HeartbeatRequest* message) noexcept
{
    if (message == nullptr)
    {
        return HeartbeatCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    HeartbeatRequest parsed_message{};
    if (!reader.ReadUInt64(&parsed_message.registration_id) ||
        !reader.ReadUInt64(&parsed_message.sent_at_unix_ms) ||
        !reader.ReadUInt32(&parsed_message.status_flags))
    {
        return MapSerializationError(reader.error());
    }

    const HeartbeatCodecErrorCode load_result = DecodeLoadSnapshot(&reader, &parsed_message.load);
    if (load_result != HeartbeatCodecErrorCode::None)
    {
        return load_result;
    }

    const HeartbeatCodecErrorCode registration_result = ValidateRegistrationId(parsed_message.registration_id);
    if (registration_result != HeartbeatCodecErrorCode::None)
    {
        return registration_result;
    }

    const HeartbeatCodecErrorCode status_result = ValidateStatusFlags(parsed_message.status_flags);
    if (status_result != HeartbeatCodecErrorCode::None)
    {
        return status_result;
    }

    const HeartbeatCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != HeartbeatCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return HeartbeatCodecErrorCode::None;
}

HeartbeatCodecErrorCode EncodeHeartbeatSuccessResponse(
    const HeartbeatSuccessResponse& message,
    std::span<std::byte> buffer) noexcept
{
    const HeartbeatCodecErrorCode registration_result = ValidateRegistrationId(message.registration_id);
    if (registration_result != HeartbeatCodecErrorCode::None)
    {
        return registration_result;
    }

    const HeartbeatCodecErrorCode timing_result =
        ValidateHeartbeatTiming(message.heartbeat_interval_ms, message.heartbeat_timeout_ms);
    if (timing_result != HeartbeatCodecErrorCode::None)
    {
        return timing_result;
    }

    BinaryWriter writer(buffer);
    if (!writer.WriteUInt64(message.registration_id) ||
        !writer.WriteUInt32(message.heartbeat_interval_ms) ||
        !writer.WriteUInt32(message.heartbeat_timeout_ms) ||
        !writer.WriteUInt64(message.server_now_unix_ms))
    {
        return MapSerializationError(writer.error());
    }

    return HeartbeatCodecErrorCode::None;
}

HeartbeatCodecErrorCode DecodeHeartbeatSuccessResponse(
    std::span<const std::byte> buffer,
    HeartbeatSuccessResponse* message) noexcept
{
    if (message == nullptr)
    {
        return HeartbeatCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    HeartbeatSuccessResponse parsed_message{};
    if (!reader.ReadUInt64(&parsed_message.registration_id) ||
        !reader.ReadUInt32(&parsed_message.heartbeat_interval_ms) ||
        !reader.ReadUInt32(&parsed_message.heartbeat_timeout_ms) ||
        !reader.ReadUInt64(&parsed_message.server_now_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const HeartbeatCodecErrorCode registration_result = ValidateRegistrationId(parsed_message.registration_id);
    if (registration_result != HeartbeatCodecErrorCode::None)
    {
        return registration_result;
    }

    const HeartbeatCodecErrorCode timing_result =
        ValidateHeartbeatTiming(parsed_message.heartbeat_interval_ms, parsed_message.heartbeat_timeout_ms);
    if (timing_result != HeartbeatCodecErrorCode::None)
    {
        return timing_result;
    }

    const HeartbeatCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != HeartbeatCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return HeartbeatCodecErrorCode::None;
}

HeartbeatCodecErrorCode EncodeHeartbeatErrorResponse(
    const HeartbeatErrorResponse& message,
    std::span<std::byte> buffer) noexcept
{
    BinaryWriter writer(buffer);
    if (!writer.WriteInt32(message.error_code) ||
        !writer.WriteUInt32(message.retry_after_ms) ||
        !writer.WriteBool(message.require_full_register))
    {
        return MapSerializationError(writer.error());
    }

    return HeartbeatCodecErrorCode::None;
}

HeartbeatCodecErrorCode DecodeHeartbeatErrorResponse(
    std::span<const std::byte> buffer,
    HeartbeatErrorResponse* message) noexcept
{
    if (message == nullptr)
    {
        return HeartbeatCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    HeartbeatErrorResponse parsed_message{};
    if (!reader.ReadInt32(&parsed_message.error_code) ||
        !reader.ReadUInt32(&parsed_message.retry_after_ms) ||
        !reader.ReadBool(&parsed_message.require_full_register))
    {
        return MapSerializationError(reader.error());
    }

    const HeartbeatCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != HeartbeatCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return HeartbeatCodecErrorCode::None;
}

} // namespace xs::net
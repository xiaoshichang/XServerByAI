#include "ProcessHeartbeatCodec.h"

#include "BinarySerialization.h"

namespace xs::net
{
namespace
{

[[nodiscard]] ProcessHeartbeatCodecErrorCode MapSerializationError(SerializationErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case SerializationErrorCode::None:
        return ProcessHeartbeatCodecErrorCode::None;
    case SerializationErrorCode::BufferTooSmall:
        return ProcessHeartbeatCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::LengthOverflow:
        return ProcessHeartbeatCodecErrorCode::LengthOverflow;
    case SerializationErrorCode::InvalidBoolValue:
        return ProcessHeartbeatCodecErrorCode::InvalidBoolValue;
    case SerializationErrorCode::InvalidArgument:
        return ProcessHeartbeatCodecErrorCode::InvalidArgument;
    }

    return ProcessHeartbeatCodecErrorCode::InvalidArgument;
}

[[nodiscard]] ProcessHeartbeatCodecErrorCode ValidateRegistrationId(std::uint64_t registration_id) noexcept
{
    if (registration_id == 0u)
    {
        return ProcessHeartbeatCodecErrorCode::InvalidRegistrationId;
    }

    return ProcessHeartbeatCodecErrorCode::None;
}

[[nodiscard]] ProcessHeartbeatCodecErrorCode ValidateStatusFlags(std::uint32_t status_flags) noexcept
{
    if (status_flags != 0u)
    {
        return ProcessHeartbeatCodecErrorCode::InvalidStatusFlags;
    }

    return ProcessHeartbeatCodecErrorCode::None;
}

[[nodiscard]] ProcessHeartbeatCodecErrorCode ValidateHeartbeatTiming(
    std::uint32_t heartbeat_interval_ms,
    std::uint32_t heartbeat_timeout_ms) noexcept
{
    if (heartbeat_interval_ms >= heartbeat_timeout_ms)
    {
        return ProcessHeartbeatCodecErrorCode::InvalidHeartbeatTiming;
    }

    return ProcessHeartbeatCodecErrorCode::None;
}

[[nodiscard]] ProcessHeartbeatCodecErrorCode EncodeLoadSnapshot(
    const LoadSnapshot& snapshot,
    BinaryWriter* writer) noexcept
{
    if (writer == nullptr)
    {
        return ProcessHeartbeatCodecErrorCode::InvalidArgument;
    }

    if (!writer->WriteUInt32(snapshot.connection_count) ||
        !writer->WriteUInt32(snapshot.session_count) ||
        !writer->WriteUInt32(snapshot.entity_count) ||
        !writer->WriteUInt32(snapshot.space_count) ||
        !writer->WriteUInt32(snapshot.load_score))
    {
        return MapSerializationError(writer->error());
    }

    return ProcessHeartbeatCodecErrorCode::None;
}

[[nodiscard]] ProcessHeartbeatCodecErrorCode DecodeLoadSnapshot(
    BinaryReader* reader,
    LoadSnapshot* snapshot) noexcept
{
    if (reader == nullptr || snapshot == nullptr)
    {
        return ProcessHeartbeatCodecErrorCode::InvalidArgument;
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
    return ProcessHeartbeatCodecErrorCode::None;
}

[[nodiscard]] ProcessHeartbeatCodecErrorCode CheckTrailingBytes(const BinaryReader& reader) noexcept
{
    if (reader.remaining() != 0u)
    {
        return ProcessHeartbeatCodecErrorCode::TrailingBytes;
    }

    return ProcessHeartbeatCodecErrorCode::None;
}

} // namespace

std::string_view ProcessHeartbeatCodecErrorMessage(ProcessHeartbeatCodecErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case ProcessHeartbeatCodecErrorCode::None:
        return "Success.";
    case ProcessHeartbeatCodecErrorCode::BufferTooSmall:
        return "Buffer does not contain enough bytes for the requested heartbeat operation.";
    case ProcessHeartbeatCodecErrorCode::LengthOverflow:
        return "Heartbeat field length exceeds the supported prefix range.";
    case ProcessHeartbeatCodecErrorCode::InvalidBoolValue:
        return "Heartbeat bool field must be encoded as 0 or 1.";
    case ProcessHeartbeatCodecErrorCode::InvalidArgument:
        return "Heartbeat codec output argument must not be null.";
    case ProcessHeartbeatCodecErrorCode::InvalidRegistrationId:
        return "Heartbeat registrationId must not be zero.";
    case ProcessHeartbeatCodecErrorCode::InvalidStatusFlags:
        return "Heartbeat statusFlags must be zero.";
    case ProcessHeartbeatCodecErrorCode::InvalidHeartbeatTiming:
        return "Heartbeat interval must be less than timeout.";
    case ProcessHeartbeatCodecErrorCode::TrailingBytes:
        return "Heartbeat buffer must not contain trailing bytes.";
    }

    return "Unknown heartbeat codec error.";
}

ProcessHeartbeatCodecErrorCode EncodeProcessHeartbeatRequest(
    const ProcessHeartbeatRequest& message,
    std::span<std::byte> buffer) noexcept
{
    const ProcessHeartbeatCodecErrorCode registration_result = ValidateRegistrationId(message.registration_id);
    if (registration_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return registration_result;
    }

    const ProcessHeartbeatCodecErrorCode status_result = ValidateStatusFlags(message.status_flags);
    if (status_result != ProcessHeartbeatCodecErrorCode::None)
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

ProcessHeartbeatCodecErrorCode DecodeProcessHeartbeatRequest(
    std::span<const std::byte> buffer,
    ProcessHeartbeatRequest* message) noexcept
{
    if (message == nullptr)
    {
        return ProcessHeartbeatCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    ProcessHeartbeatRequest parsed_message{};
    if (!reader.ReadUInt64(&parsed_message.registration_id) ||
        !reader.ReadUInt64(&parsed_message.sent_at_unix_ms) ||
        !reader.ReadUInt32(&parsed_message.status_flags))
    {
        return MapSerializationError(reader.error());
    }

    const ProcessHeartbeatCodecErrorCode load_result = DecodeLoadSnapshot(&reader, &parsed_message.load);
    if (load_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return load_result;
    }

    const ProcessHeartbeatCodecErrorCode registration_result = ValidateRegistrationId(parsed_message.registration_id);
    if (registration_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return registration_result;
    }

    const ProcessHeartbeatCodecErrorCode status_result = ValidateStatusFlags(parsed_message.status_flags);
    if (status_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return status_result;
    }

    const ProcessHeartbeatCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return ProcessHeartbeatCodecErrorCode::None;
}

ProcessHeartbeatCodecErrorCode EncodeProcessHeartbeatSuccessResponse(
    const ProcessHeartbeatSuccessResponse& message,
    std::span<std::byte> buffer) noexcept
{
    const ProcessHeartbeatCodecErrorCode registration_result = ValidateRegistrationId(message.registration_id);
    if (registration_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return registration_result;
    }

    const ProcessHeartbeatCodecErrorCode timing_result =
        ValidateHeartbeatTiming(message.heartbeat_interval_ms, message.heartbeat_timeout_ms);
    if (timing_result != ProcessHeartbeatCodecErrorCode::None)
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

    return ProcessHeartbeatCodecErrorCode::None;
}

ProcessHeartbeatCodecErrorCode DecodeProcessHeartbeatSuccessResponse(
    std::span<const std::byte> buffer,
    ProcessHeartbeatSuccessResponse* message) noexcept
{
    if (message == nullptr)
    {
        return ProcessHeartbeatCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    ProcessHeartbeatSuccessResponse parsed_message{};
    if (!reader.ReadUInt64(&parsed_message.registration_id) ||
        !reader.ReadUInt32(&parsed_message.heartbeat_interval_ms) ||
        !reader.ReadUInt32(&parsed_message.heartbeat_timeout_ms) ||
        !reader.ReadUInt64(&parsed_message.server_now_unix_ms))
    {
        return MapSerializationError(reader.error());
    }

    const ProcessHeartbeatCodecErrorCode registration_result = ValidateRegistrationId(parsed_message.registration_id);
    if (registration_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return registration_result;
    }

    const ProcessHeartbeatCodecErrorCode timing_result =
        ValidateHeartbeatTiming(parsed_message.heartbeat_interval_ms, parsed_message.heartbeat_timeout_ms);
    if (timing_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return timing_result;
    }

    const ProcessHeartbeatCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return ProcessHeartbeatCodecErrorCode::None;
}

ProcessHeartbeatCodecErrorCode EncodeProcessHeartbeatErrorResponse(
    const ProcessHeartbeatErrorResponse& message,
    std::span<std::byte> buffer) noexcept
{
    BinaryWriter writer(buffer);
    if (!writer.WriteInt32(message.error_code) ||
        !writer.WriteUInt32(message.retry_after_ms) ||
        !writer.WriteBool(message.require_full_register))
    {
        return MapSerializationError(writer.error());
    }

    return ProcessHeartbeatCodecErrorCode::None;
}

ProcessHeartbeatCodecErrorCode DecodeProcessHeartbeatErrorResponse(
    std::span<const std::byte> buffer,
    ProcessHeartbeatErrorResponse* message) noexcept
{
    if (message == nullptr)
    {
        return ProcessHeartbeatCodecErrorCode::InvalidArgument;
    }

    *message = {};

    BinaryReader reader(buffer);
    ProcessHeartbeatErrorResponse parsed_message{};
    if (!reader.ReadInt32(&parsed_message.error_code) ||
        !reader.ReadUInt32(&parsed_message.retry_after_ms) ||
        !reader.ReadBool(&parsed_message.require_full_register))
    {
        return MapSerializationError(reader.error());
    }

    const ProcessHeartbeatCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != ProcessHeartbeatCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return ProcessHeartbeatCodecErrorCode::None;
}

} // namespace xs::net
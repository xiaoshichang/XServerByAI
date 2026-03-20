#include "ClusterControlCodec.h"

#include "BinarySerialization.h"

namespace xs::net
{
namespace
{

[[nodiscard]] ClusterControlCodecErrorCode MapSerializationError(SerializationErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case SerializationErrorCode::None:
        return ClusterControlCodecErrorCode::None;
    case SerializationErrorCode::BufferTooSmall:
        return ClusterControlCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::LengthOverflow:
        return ClusterControlCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::InvalidBoolValue:
        return ClusterControlCodecErrorCode::InvalidBoolValue;
    case SerializationErrorCode::InvalidArgument:
        return ClusterControlCodecErrorCode::InvalidArgument;
    }

    return ClusterControlCodecErrorCode::InvalidArgument;
}

[[nodiscard]] ClusterControlCodecErrorCode ValidateClusterReadyNotify(const ClusterReadyNotify& message) noexcept
{
    if (message.status_flags != 0u)
    {
        return ClusterControlCodecErrorCode::InvalidReadyStatusFlags;
    }

    return ClusterControlCodecErrorCode::None;
}

[[nodiscard]] ClusterControlCodecErrorCode CheckTrailingBytes(const BinaryReader& reader) noexcept
{
    if (reader.remaining() != 0u)
    {
        return ClusterControlCodecErrorCode::TrailingBytes;
    }

    return ClusterControlCodecErrorCode::None;
}

} // namespace

std::string_view ClusterControlCodecErrorMessage(ClusterControlCodecErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case ClusterControlCodecErrorCode::None:
        return "Success.";
    case ClusterControlCodecErrorCode::BufferTooSmall:
        return "Buffer does not contain enough bytes for the requested cluster-control operation.";
    case ClusterControlCodecErrorCode::InvalidBoolValue:
        return "Cluster-control bool field must be encoded as 0 or 1.";
    case ClusterControlCodecErrorCode::InvalidArgument:
        return "Cluster-control codec output argument must not be null.";
    case ClusterControlCodecErrorCode::InvalidReadyStatusFlags:
        return "ClusterReadyNotify statusFlags must be zero.";
    case ClusterControlCodecErrorCode::TrailingBytes:
        return "Cluster-control buffer must not contain trailing bytes.";
    }

    return "Unknown cluster-control codec error.";
}

ClusterControlCodecErrorCode EncodeClusterReadyNotify(
    const ClusterReadyNotify& message,
    std::span<std::byte> buffer) noexcept
{
    const ClusterControlCodecErrorCode validation_result = ValidateClusterReadyNotify(message);
    if (validation_result != ClusterControlCodecErrorCode::None)
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

    return ClusterControlCodecErrorCode::None;
}

ClusterControlCodecErrorCode DecodeClusterReadyNotify(
    std::span<const std::byte> buffer,
    ClusterReadyNotify* message) noexcept
{
    if (message == nullptr)
    {
        return ClusterControlCodecErrorCode::InvalidArgument;
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

    const ClusterControlCodecErrorCode validation_result = ValidateClusterReadyNotify(parsed_message);
    if (validation_result != ClusterControlCodecErrorCode::None)
    {
        return validation_result;
    }

    const ClusterControlCodecErrorCode trailing_result = CheckTrailingBytes(reader);
    if (trailing_result != ClusterControlCodecErrorCode::None)
    {
        return trailing_result;
    }

    *message = parsed_message;
    return ClusterControlCodecErrorCode::None;
}

} // namespace xs::net

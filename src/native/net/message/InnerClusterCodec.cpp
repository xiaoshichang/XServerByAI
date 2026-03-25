#include "InnerClusterCodec.h"

#include "BinarySerialization.h"

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
        return InnerClusterCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::InvalidBoolValue:
        return InnerClusterCodecErrorCode::InvalidBoolValue;
    case SerializationErrorCode::InvalidArgument:
        return InnerClusterCodecErrorCode::InvalidArgument;
    }

    return InnerClusterCodecErrorCode::InvalidArgument;
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

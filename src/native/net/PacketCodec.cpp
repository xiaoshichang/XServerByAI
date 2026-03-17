#include "PacketCodec.h"

#include "BinarySerialization.h"

#include <cstring>
#include <limits>

namespace xs::net
{
namespace
{

[[nodiscard]] PacketCodecErrorCode MapSerializationError(SerializationErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case SerializationErrorCode::None:
        return PacketCodecErrorCode::None;
    case SerializationErrorCode::BufferTooSmall:
        return PacketCodecErrorCode::BufferTooSmall;
    case SerializationErrorCode::LengthOverflow:
        return PacketCodecErrorCode::LengthOverflow;
    case SerializationErrorCode::InvalidArgument:
        return PacketCodecErrorCode::InvalidArgument;
    case SerializationErrorCode::InvalidBoolValue:
        return PacketCodecErrorCode::InvalidArgument;
    }

    return PacketCodecErrorCode::InvalidArgument;
}

} // namespace

std::string_view PacketCodecErrorMessage(PacketCodecErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case PacketCodecErrorCode::None:
        return "Success.";
    case PacketCodecErrorCode::BufferTooSmall:
        return "Buffer does not contain enough bytes for the requested packet operation.";
    case PacketCodecErrorCode::LengthOverflow:
        return "Packet payload length exceeds the supported range.";
    case PacketCodecErrorCode::InvalidMagic:
        return "Packet header magic does not match the protocol constant.";
    case PacketCodecErrorCode::UnsupportedVersion:
        return "Packet header version is not supported.";
    case PacketCodecErrorCode::InvalidFlags:
        return "Packet header contains undefined flag bits.";
    case PacketCodecErrorCode::LengthMismatch:
        return "Packet header length does not match the provided payload bytes.";
    case PacketCodecErrorCode::InvalidArgument:
        return "Packet codec argument must not be null.";
    }

    return "Unknown packet codec error.";
}

bool IsValidPacketFlags(std::uint16_t flags) noexcept
{
    return (flags & static_cast<std::uint16_t>(~kPacketDefinedFlagMask)) == 0u;
}

PacketHeader MakePacketHeader(
    std::uint32_t msg_id,
    std::uint32_t seq,
    std::uint16_t flags,
    std::uint32_t payload_length) noexcept
{
    return PacketHeader{
        .magic = kPacketMagic,
        .version = kPacketVersion,
        .flags = flags,
        .length = payload_length,
        .msg_id = msg_id,
        .seq = seq,
    };
}

PacketCodecErrorCode ValidatePacketHeader(const PacketHeader& header) noexcept
{
    if (header.magic != kPacketMagic)
    {
        return PacketCodecErrorCode::InvalidMagic;
    }

    if (header.version != kPacketVersion)
    {
        return PacketCodecErrorCode::UnsupportedVersion;
    }

    if (!IsValidPacketFlags(header.flags))
    {
        return PacketCodecErrorCode::InvalidFlags;
    }

    return PacketCodecErrorCode::None;
}

PacketCodecErrorCode GetPacketWireSize(std::size_t payload_size, std::size_t* wire_size) noexcept
{
    if (wire_size == nullptr)
    {
        return PacketCodecErrorCode::InvalidArgument;
    }

    if (payload_size > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return PacketCodecErrorCode::LengthOverflow;
    }

    if (payload_size > std::numeric_limits<std::size_t>::max() - kPacketHeaderSize)
    {
        return PacketCodecErrorCode::LengthOverflow;
    }

    *wire_size = kPacketHeaderSize + payload_size;
    return PacketCodecErrorCode::None;
}

PacketCodecErrorCode WritePacketHeader(const PacketHeader& header, std::span<std::byte> buffer) noexcept
{
    const PacketCodecErrorCode validation_result = ValidatePacketHeader(header);
    if (validation_result != PacketCodecErrorCode::None)
    {
        return validation_result;
    }

    if (buffer.size() < kPacketHeaderSize)
    {
        return PacketCodecErrorCode::BufferTooSmall;
    }

    BinaryWriter writer(buffer.first(kPacketHeaderSize));
    if (!writer.WriteUInt32(header.magic) ||
        !writer.WriteUInt16(header.version) ||
        !writer.WriteUInt16(header.flags) ||
        !writer.WriteUInt32(header.length) ||
        !writer.WriteUInt32(header.msg_id) ||
        !writer.WriteUInt32(header.seq))
    {
        return MapSerializationError(writer.error());
    }

    return PacketCodecErrorCode::None;
}

PacketCodecErrorCode ReadPacketHeader(std::span<const std::byte> buffer, PacketHeader* header) noexcept
{
    if (header == nullptr)
    {
        return PacketCodecErrorCode::InvalidArgument;
    }

    *header = {};
    if (buffer.size() < kPacketHeaderSize)
    {
        return PacketCodecErrorCode::BufferTooSmall;
    }

    BinaryReader reader(buffer.first(kPacketHeaderSize));
    PacketHeader parsed_header{};
    if (!reader.ReadUInt32(&parsed_header.magic) ||
        !reader.ReadUInt16(&parsed_header.version) ||
        !reader.ReadUInt16(&parsed_header.flags) ||
        !reader.ReadUInt32(&parsed_header.length) ||
        !reader.ReadUInt32(&parsed_header.msg_id) ||
        !reader.ReadUInt32(&parsed_header.seq))
    {
        return MapSerializationError(reader.error());
    }

    const PacketCodecErrorCode validation_result = ValidatePacketHeader(parsed_header);
    if (validation_result != PacketCodecErrorCode::None)
    {
        return validation_result;
    }

    *header = parsed_header;
    return PacketCodecErrorCode::None;
}

PacketCodecErrorCode EncodePacket(
    const PacketHeader& header,
    std::span<const std::byte> payload,
    std::span<std::byte> buffer) noexcept
{
    const PacketCodecErrorCode validation_result = ValidatePacketHeader(header);
    if (validation_result != PacketCodecErrorCode::None)
    {
        return validation_result;
    }

    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return PacketCodecErrorCode::LengthOverflow;
    }

    if (header.length != static_cast<std::uint32_t>(payload.size()))
    {
        return PacketCodecErrorCode::LengthMismatch;
    }

    std::size_t wire_size = 0;
    const PacketCodecErrorCode wire_size_result = GetPacketWireSize(payload.size(), &wire_size);
    if (wire_size_result != PacketCodecErrorCode::None)
    {
        return wire_size_result;
    }

    if (buffer.size() < wire_size)
    {
        return PacketCodecErrorCode::BufferTooSmall;
    }

    const PacketCodecErrorCode write_header_result = WritePacketHeader(header, buffer.first(kPacketHeaderSize));
    if (write_header_result != PacketCodecErrorCode::None)
    {
        return write_header_result;
    }

    if (!payload.empty())
    {
        std::memcpy(buffer.data() + kPacketHeaderSize, payload.data(), payload.size());
    }

    return PacketCodecErrorCode::None;
}

PacketCodecErrorCode DecodePacket(std::span<const std::byte> buffer, PacketView* packet) noexcept
{
    if (packet == nullptr)
    {
        return PacketCodecErrorCode::InvalidArgument;
    }

    *packet = {};

    PacketHeader header{};
    const PacketCodecErrorCode read_header_result = ReadPacketHeader(buffer, &header);
    if (read_header_result != PacketCodecErrorCode::None)
    {
        return read_header_result;
    }

    std::size_t expected_wire_size = 0;
    const PacketCodecErrorCode wire_size_result =
        GetPacketWireSize(static_cast<std::size_t>(header.length), &expected_wire_size);
    if (wire_size_result != PacketCodecErrorCode::None)
    {
        return wire_size_result;
    }

    if (buffer.size() != expected_wire_size)
    {
        return PacketCodecErrorCode::LengthMismatch;
    }

    packet->header = header;
    packet->payload = buffer.subspan(kPacketHeaderSize, static_cast<std::size_t>(header.length));
    return PacketCodecErrorCode::None;
}

} // namespace xs::net
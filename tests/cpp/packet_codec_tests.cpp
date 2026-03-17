#include "PacketCodec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace
{

int g_failures = 0;

void Check(bool condition, const char* expression, const char* message = nullptr)
{
    if (condition)
    {
        return;
    }

    std::cerr << "Check failed: " << expression;
    if (message != nullptr)
    {
        std::cerr << " (" << message << ")";
    }
    std::cerr << '\n';
    ++g_failures;
}

#define XS_CHECK(expr) Check((expr), #expr)
#define XS_CHECK_MSG(expr, message) Check((expr), #expr, (message))

[[nodiscard]] std::vector<std::byte> BytesFromText(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char character : text)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return bytes;
}

[[nodiscard]] bool ByteSpanEqualsSpan(std::span<const std::byte> left, std::span<const std::byte> right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index] != right[index])
        {
            return false;
        }
    }

    return true;
}

void TestEncodeProducesExpectedWireBytes()
{
    const auto payload = BytesFromText("ok");
    const auto flags = static_cast<std::uint16_t>(xs::net::PacketFlag::Response);
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(2000u, 42u, flags, static_cast<std::uint32_t>(payload.size()));

    std::size_t wire_size = 0;
    XS_CHECK(xs::net::GetPacketWireSize(payload.size(), &wire_size) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(wire_size == xs::net::kPacketHeaderSize + payload.size());

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodePacket(header, payload, buffer) == xs::net::PacketCodecErrorCode::None);

    const std::array<std::byte, 22> expected{
        std::byte{0x47},
        std::byte{0x53},
        std::byte{0x50},
        std::byte{0x52},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x02},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x07},
        std::byte{0xD0},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x2A},
        std::byte{0x6F},
        std::byte{0x6B},
    };

    XS_CHECK(ByteSpanEqualsSpan(buffer, expected));

    std::array<std::byte, xs::net::kPacketHeaderSize> header_buffer{};
    XS_CHECK(xs::net::WritePacketHeader(header, header_buffer) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(ByteSpanEqualsSpan(header_buffer, std::span<const std::byte>(expected.data(), xs::net::kPacketHeaderSize)));

    xs::net::PacketHeader parsed_header{};
    XS_CHECK(xs::net::ReadPacketHeader(buffer, &parsed_header) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(parsed_header.magic == xs::net::kPacketMagic);
    XS_CHECK(parsed_header.version == xs::net::kPacketVersion);
    XS_CHECK(parsed_header.flags == flags);
    XS_CHECK(parsed_header.length == payload.size());
    XS_CHECK(parsed_header.msg_id == 2000u);
    XS_CHECK(parsed_header.seq == 42u);
}

void TestDecodeRoundTripUsesZeroCopyPayloadView()
{
    const auto payload = BytesFromText("payload");
    const auto flags = static_cast<std::uint16_t>(xs::net::PacketFlag::Compressed);
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(3001u, 7u, flags, static_cast<std::uint32_t>(payload.size()));

    std::size_t wire_size = 0;
    XS_CHECK(xs::net::GetPacketWireSize(payload.size(), &wire_size) == xs::net::PacketCodecErrorCode::None);

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodePacket(header, payload, buffer) == xs::net::PacketCodecErrorCode::None);

    xs::net::PacketView packet;
    XS_CHECK(xs::net::DecodePacket(buffer, &packet) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(packet.header.magic == xs::net::kPacketMagic);
    XS_CHECK(packet.header.version == xs::net::kPacketVersion);
    XS_CHECK(packet.header.flags == flags);
    XS_CHECK(packet.header.length == payload.size());
    XS_CHECK(packet.header.msg_id == 3001u);
    XS_CHECK(packet.header.seq == 7u);
    XS_CHECK(packet.payload.size() == payload.size());
    XS_CHECK(packet.payload.data() == buffer.data() + xs::net::kPacketHeaderSize);
    XS_CHECK(ByteSpanEqualsSpan(packet.payload, payload));
}

void TestDecodeRejectsMalformedPackets()
{
    std::array<std::byte, 10> too_small{};
    xs::net::PacketView packet;

    XS_CHECK(xs::net::DecodePacket(too_small, &packet) == xs::net::PacketCodecErrorCode::BufferTooSmall);

    const auto payload = BytesFromText("abc");
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(12u, 99u, 0u, static_cast<std::uint32_t>(payload.size()));
    std::size_t wire_size = 0;
    XS_CHECK(xs::net::GetPacketWireSize(payload.size(), &wire_size) == xs::net::PacketCodecErrorCode::None);

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodePacket(header, payload, buffer) == xs::net::PacketCodecErrorCode::None);

    auto invalid_magic = buffer;
    invalid_magic[0] = std::byte{0x00};
    XS_CHECK(xs::net::DecodePacket(invalid_magic, &packet) == xs::net::PacketCodecErrorCode::InvalidMagic);

    auto invalid_flags = buffer;
    invalid_flags[7] = std::byte{0x08};
    XS_CHECK(xs::net::DecodePacket(invalid_flags, &packet) == xs::net::PacketCodecErrorCode::InvalidFlags);
    XS_CHECK(
        xs::net::PacketCodecErrorMessage(xs::net::PacketCodecErrorCode::InvalidFlags) ==
        std::string_view("Packet header contains undefined flag bits."));

    const std::span<const std::byte> truncated(buffer.data(), buffer.size() - 1u);
    XS_CHECK(xs::net::DecodePacket(truncated, &packet) == xs::net::PacketCodecErrorCode::LengthMismatch);

    auto trailing = buffer;
    trailing.push_back(std::byte{0xFF});
    XS_CHECK(xs::net::DecodePacket(trailing, &packet) == xs::net::PacketCodecErrorCode::LengthMismatch);
}

void TestEncodeRejectsInvalidArgumentsAndLengthErrors()
{
    std::size_t wire_size = 0;

    XS_CHECK(xs::net::GetPacketWireSize(0u, nullptr) == xs::net::PacketCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::GetPacketWireSize(
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1u,
            &wire_size) == xs::net::PacketCodecErrorCode::LengthOverflow);

    const auto payload = BytesFromText("abc");
    const xs::net::PacketHeader short_header = xs::net::MakePacketHeader(1u, 2u, 0u, static_cast<std::uint32_t>(payload.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + 2> short_buffer{};
    XS_CHECK(xs::net::EncodePacket(short_header, payload, short_buffer) == xs::net::PacketCodecErrorCode::BufferTooSmall);

    const xs::net::PacketHeader mismatch_header = xs::net::MakePacketHeader(1u, 2u, 0u, 5u);
    std::array<std::byte, 32> large_buffer{};
    XS_CHECK(xs::net::EncodePacket(mismatch_header, payload, large_buffer) == xs::net::PacketCodecErrorCode::LengthMismatch);

    std::size_t exact_wire_size = 0;
    XS_CHECK(xs::net::GetPacketWireSize(payload.size(), &exact_wire_size) == xs::net::PacketCodecErrorCode::None);
    std::vector<std::byte> valid_buffer(exact_wire_size);
    XS_CHECK(xs::net::EncodePacket(short_header, payload, valid_buffer) == xs::net::PacketCodecErrorCode::None);

    XS_CHECK(xs::net::DecodePacket(valid_buffer, nullptr) == xs::net::PacketCodecErrorCode::InvalidArgument);
    XS_CHECK(xs::net::ReadPacketHeader(valid_buffer, nullptr) == xs::net::PacketCodecErrorCode::InvalidArgument);
}

void TestHeaderValidationReturnsConcreteErrorCodes()
{
    xs::net::PacketHeader header = xs::net::MakePacketHeader(10u, 11u, 0u, 0u);
    XS_CHECK(xs::net::ValidatePacketHeader(header) == xs::net::PacketCodecErrorCode::None);

    header.magic = 0u;
    XS_CHECK(xs::net::ValidatePacketHeader(header) == xs::net::PacketCodecErrorCode::InvalidMagic);

    header = xs::net::MakePacketHeader(10u, 11u, 0u, 0u);
    header.version = 99u;
    XS_CHECK(xs::net::ValidatePacketHeader(header) == xs::net::PacketCodecErrorCode::UnsupportedVersion);

    header = xs::net::MakePacketHeader(10u, 11u, 0x0080u, 0u);
    XS_CHECK(xs::net::ValidatePacketHeader(header) == xs::net::PacketCodecErrorCode::InvalidFlags);
}

} // namespace

int main()
{
    TestEncodeProducesExpectedWireBytes();
    TestDecodeRoundTripUsesZeroCopyPayloadView();
    TestDecodeRejectsMalformedPackets();
    TestEncodeRejectsInvalidArgumentsAndLengthErrors();
    TestHeaderValidationReturnsConcreteErrorCodes();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " packet codec test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
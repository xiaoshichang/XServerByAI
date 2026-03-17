#include "BinarySerialization.h"
#include "ByteOrder.h"

#include <bit>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
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

void TestByteOrderRoundTrip()
{
    constexpr std::uint16_t u16 = 0x1234u;
    constexpr std::uint32_t u32 = 0x89ABCDEFu;
    constexpr std::uint64_t u64 = 0x0102030405060708ull;
    constexpr std::int32_t i32 = -2024;

    XS_CHECK(xs::net::NetworkToHost(xs::net::HostToNetwork(u16)) == u16);
    XS_CHECK(xs::net::NetworkToHost(xs::net::HostToNetwork(u32)) == u32);
    XS_CHECK(xs::net::NetworkToHost(xs::net::HostToNetwork(u64)) == u64);
    XS_CHECK(xs::net::NetworkToHost(xs::net::HostToNetwork(i32)) == i32);

    if constexpr (std::endian::native == std::endian::little)
    {
        XS_CHECK(xs::net::HostToNetwork(u16) == 0x3412u);
        XS_CHECK(xs::net::HostToNetwork(u32) == 0xEFCDAB89u);
    }
    else
    {
        XS_CHECK(xs::net::HostToNetwork(u16) == u16);
        XS_CHECK(xs::net::HostToNetwork(u32) == u32);
    }
}

void TestBinaryWriterProducesExpectedWireBytes()
{
    std::array<std::byte, 29> buffer{};
    xs::net::BinaryWriter writer(buffer);

    const std::array<std::byte, 2> payload{
        std::byte{0xAA},
        std::byte{0xBB},
    };

    XS_CHECK(writer.WriteUInt16(0x1234u));
    XS_CHECK(writer.WriteUInt32(0x89ABCDEFu));
    XS_CHECK(writer.WriteUInt64(0x0102030405060708ull));
    XS_CHECK(writer.WriteInt32(-2));
    XS_CHECK(writer.WriteBool(true));
    XS_CHECK(writer.WriteString16("Hi"));
    XS_CHECK(writer.WriteLengthPrefixedBytes32(payload));
    XS_CHECK(writer.offset() == buffer.size());

    const std::array<std::byte, 29> expected{
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x89},
        std::byte{0xAB},
        std::byte{0xCD},
        std::byte{0xEF},
        std::byte{0x01},
        std::byte{0x02},
        std::byte{0x03},
        std::byte{0x04},
        std::byte{0x05},
        std::byte{0x06},
        std::byte{0x07},
        std::byte{0x08},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFF},
        std::byte{0xFE},
        std::byte{0x01},
        std::byte{0x00},
        std::byte{0x02},
        std::byte{0x48},
        std::byte{0x69},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x00},
        std::byte{0x02},
        std::byte{0xAA},
        std::byte{0xBB},
    };

    XS_CHECK(writer.written().size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        XS_CHECK(writer.written()[index] == expected[index]);
    }
}

void TestBinaryReaderRoundTrip()
{
    std::array<std::byte, 32> buffer{};
    xs::net::BinaryWriter writer(buffer);
    const std::array<std::byte, 3> payload{std::byte{0x10}, std::byte{0x11}, std::byte{0x12}};

    XS_CHECK(writer.WriteUInt8(7u));
    XS_CHECK(writer.WriteUInt16(0xBEEFu));
    XS_CHECK(writer.WriteUInt32(42u));
    XS_CHECK(writer.WriteUInt64(99u));
    XS_CHECK(writer.WriteInt32(-123));
    XS_CHECK(writer.WriteBool(false));
    XS_CHECK(writer.WriteString16("Gate0"));
    XS_CHECK(writer.WriteLengthPrefixedBytes16(payload));

    xs::net::BinaryReader reader(writer.written());
    std::uint8_t u8 = 0;
    std::uint16_t u16 = 0;
    std::uint32_t u32 = 0;
    std::uint64_t u64 = 0;
    std::int32_t i32 = 0;
    bool boolean_value = true;
    std::string node_id;
    std::span<const std::byte> read_payload;

    XS_CHECK(reader.ReadUInt8(&u8));
    XS_CHECK(reader.ReadUInt16(&u16));
    XS_CHECK(reader.ReadUInt32(&u32));
    XS_CHECK(reader.ReadUInt64(&u64));
    XS_CHECK(reader.ReadInt32(&i32));
    XS_CHECK(reader.ReadBool(&boolean_value));
    XS_CHECK(reader.ReadString16(&node_id));
    XS_CHECK(reader.ReadLengthPrefixedBytes16(&read_payload));

    XS_CHECK(u8 == 7u);
    XS_CHECK(u16 == 0xBEEFu);
    XS_CHECK(u32 == 42u);
    XS_CHECK(u64 == 99u);
    XS_CHECK(i32 == -123);
    XS_CHECK(!boolean_value);
    XS_CHECK(node_id == "Gate0");
    XS_CHECK(read_payload.size() == payload.size());
    for (std::size_t index = 0; index < payload.size(); ++index)
    {
        XS_CHECK(read_payload[index] == payload[index]);
    }
    XS_CHECK(reader.remaining() == 0);
}

void TestWriterRejectsOverflowAndShortBufferWithoutAdvancing()
{
    std::array<std::byte, 3> small_buffer{};
    xs::net::BinaryWriter small_writer(small_buffer);

    XS_CHECK(!small_writer.WriteUInt32(1u));
    XS_CHECK(small_writer.error() == xs::net::SerializationErrorCode::BufferTooSmall);
    XS_CHECK(small_writer.offset() == 0);

    std::vector<char> too_long(static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u, 'x');
    std::array<std::byte, 8> overflow_buffer{};
    xs::net::BinaryWriter overflow_writer(overflow_buffer);
    XS_CHECK(!overflow_writer.WriteString16(std::string_view(too_long.data(), too_long.size())));
    XS_CHECK(overflow_writer.error() == xs::net::SerializationErrorCode::LengthOverflow);
    XS_CHECK(overflow_writer.offset() == 0);
}

void TestReaderRejectsInvalidBoolAndTruncatedPayloadWithoutAdvancing()
{
    const std::array<std::byte, 1> invalid_bool_buffer{std::byte{0x02}};
    xs::net::BinaryReader bool_reader(invalid_bool_buffer);
    bool boolean_value = false;

    XS_CHECK(!bool_reader.ReadBool(&boolean_value));
    XS_CHECK(bool_reader.error() == xs::net::SerializationErrorCode::InvalidBoolValue);
    XS_CHECK(bool_reader.offset() == 0);

    const std::array<std::byte, 3> truncated_buffer{std::byte{0x00}, std::byte{0x04}, std::byte{0xAA}};
    xs::net::BinaryReader truncated_reader(truncated_buffer);
    std::span<const std::byte> payload;

    XS_CHECK(!truncated_reader.ReadLengthPrefixedBytes16(&payload));
    XS_CHECK(truncated_reader.error() == xs::net::SerializationErrorCode::BufferTooSmall);
    XS_CHECK(truncated_reader.offset() == 0);
}

void TestReaderRejectsNullOutputs()
{
    const std::array<std::byte, 2> buffer{std::byte{0x00}, std::byte{0x01}};
    xs::net::BinaryReader reader(buffer);

    XS_CHECK(!reader.ReadUInt16(nullptr));
    XS_CHECK(reader.error() == xs::net::SerializationErrorCode::InvalidArgument);
    XS_CHECK(reader.offset() == 0);
    XS_CHECK(
        xs::net::SerializationErrorMessage(xs::net::SerializationErrorCode::InvalidArgument) ==
        std::string_view("Output argument must not be null."));
}

} // namespace

int main()
{
    TestByteOrderRoundTrip();
    TestBinaryWriterProducesExpectedWireBytes();
    TestBinaryReaderRoundTrip();
    TestWriterRejectsOverflowAndShortBufferWithoutAdvancing();
    TestReaderRejectsInvalidBoolAndTruncatedPayloadWithoutAdvancing();
    TestReaderRejectsNullOutputs();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " net serialization test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
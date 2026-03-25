#include "message/HeartbeatCodec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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

[[nodiscard]] xs::net::LoadSnapshot MakeLoadSnapshot(
    std::uint32_t connection_count,
    std::uint32_t session_count,
    std::uint32_t entity_count,
    std::uint32_t space_count,
    std::uint32_t load_score)
{
    return xs::net::LoadSnapshot{
        .connection_count = connection_count,
        .session_count = session_count,
        .entity_count = entity_count,
        .space_count = space_count,
        .load_score = load_score,
    };
}

void CheckLoadSnapshotEquals(const xs::net::LoadSnapshot& actual, const xs::net::LoadSnapshot& expected)
{
    XS_CHECK(actual.connection_count == expected.connection_count);
    XS_CHECK(actual.session_count == expected.session_count);
    XS_CHECK(actual.entity_count == expected.entity_count);
    XS_CHECK(actual.space_count == expected.space_count);
    XS_CHECK(actual.load_score == expected.load_score);
}

void TestEncodeRequestProducesExpectedWireBytesAndRoundTrip()
{
    const xs::net::HeartbeatRequest request{
        .sent_at_unix_ms = 0x1112131415161718ull,
        .status_flags = 0u,
        .load = MakeLoadSnapshot(1u, 2u, 3u, 4u, 5u),
    };

    std::array<std::byte, xs::net::kHeartbeatRequestSize> buffer{};
    XS_CHECK(xs::net::EncodeHeartbeatRequest(request, buffer) == xs::net::HeartbeatCodecErrorCode::None);

    const std::array<std::byte, xs::net::kHeartbeatRequestSize> expected{
        std::byte{0x11}, std::byte{0x12}, std::byte{0x13}, std::byte{0x14},
        std::byte{0x15}, std::byte{0x16}, std::byte{0x17}, std::byte{0x18},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x03},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x04},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x05},
    };
    XS_CHECK(ByteSpanEqualsSpan(buffer, expected));

    xs::net::HeartbeatRequest decoded{};
    XS_CHECK(xs::net::DecodeHeartbeatRequest(buffer, &decoded) == xs::net::HeartbeatCodecErrorCode::None);
    XS_CHECK(decoded.sent_at_unix_ms == request.sent_at_unix_ms);
    XS_CHECK(decoded.status_flags == 0u);
    CheckLoadSnapshotEquals(decoded.load, request.load);
}

void TestEncodeSuccessResponseRoundTrip()
{
    const xs::net::HeartbeatSuccessResponse success{
        .heartbeat_interval_ms = 5000u,
        .heartbeat_timeout_ms = 15000u,
        .server_now_unix_ms = 0x2122232425262728ull,
    };

    std::array<std::byte, xs::net::kHeartbeatSuccessResponseSize> success_buffer{};
    XS_CHECK(
        xs::net::EncodeHeartbeatSuccessResponse(success, success_buffer) ==
        xs::net::HeartbeatCodecErrorCode::None);

    const std::array<std::byte, xs::net::kHeartbeatSuccessResponseSize> expected_success{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x13}, std::byte{0x88},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x3A}, std::byte{0x98},
        std::byte{0x21}, std::byte{0x22}, std::byte{0x23}, std::byte{0x24},
        std::byte{0x25}, std::byte{0x26}, std::byte{0x27}, std::byte{0x28},
    };
    XS_CHECK(ByteSpanEqualsSpan(success_buffer, expected_success));

    xs::net::HeartbeatSuccessResponse decoded_success{};
    XS_CHECK(
        xs::net::DecodeHeartbeatSuccessResponse(success_buffer, &decoded_success) ==
        xs::net::HeartbeatCodecErrorCode::None);
    XS_CHECK(decoded_success.heartbeat_interval_ms == success.heartbeat_interval_ms);
    XS_CHECK(decoded_success.heartbeat_timeout_ms == success.heartbeat_timeout_ms);
    XS_CHECK(decoded_success.server_now_unix_ms == success.server_now_unix_ms);
}

void TestRejectsSemanticViolationsAndMalformedBuffers()
{
    const xs::net::HeartbeatRequest request{
        .sent_at_unix_ms = 0x1112131415161718ull,
        .status_flags = 0u,
        .load = MakeLoadSnapshot(1u, 2u, 3u, 4u, 5u),
    };

    std::array<std::byte, xs::net::kHeartbeatRequestSize> request_buffer{};
    XS_CHECK(xs::net::EncodeHeartbeatRequest(request, request_buffer) == xs::net::HeartbeatCodecErrorCode::None);

    auto invalid_status = request_buffer;
    invalid_status[11] = std::byte{0x01};
    xs::net::HeartbeatRequest decoded_request{};
    XS_CHECK(
        xs::net::DecodeHeartbeatRequest(invalid_status, &decoded_request) ==
        xs::net::HeartbeatCodecErrorCode::InvalidStatusFlags);

    const std::span<const std::byte> truncated_request(request_buffer.data(), request_buffer.size() - 1u);
    XS_CHECK(
        xs::net::DecodeHeartbeatRequest(truncated_request, &decoded_request) ==
        xs::net::HeartbeatCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing_request(request_buffer.begin(), request_buffer.end());
    trailing_request.push_back(std::byte{0xFF});
    XS_CHECK(
        xs::net::DecodeHeartbeatRequest(trailing_request, &decoded_request) ==
        xs::net::HeartbeatCodecErrorCode::TrailingBytes);

    const xs::net::HeartbeatSuccessResponse success{
        .heartbeat_interval_ms = 5000u,
        .heartbeat_timeout_ms = 15000u,
        .server_now_unix_ms = 0x2122232425262728ull,
    };
    std::array<std::byte, xs::net::kHeartbeatSuccessResponseSize> success_buffer{};
    XS_CHECK(
        xs::net::EncodeHeartbeatSuccessResponse(success, success_buffer) ==
        xs::net::HeartbeatCodecErrorCode::None);
    const auto valid_success_buffer = success_buffer;
    success_buffer[0] = std::byte{0x00};
    success_buffer[1] = std::byte{0x00};
    success_buffer[2] = std::byte{0x3A};
    success_buffer[3] = std::byte{0x98};

    xs::net::HeartbeatSuccessResponse decoded_success{};
    XS_CHECK(
        xs::net::DecodeHeartbeatSuccessResponse(success_buffer, &decoded_success) ==
        xs::net::HeartbeatCodecErrorCode::InvalidHeartbeatTiming);

    std::vector<std::byte> trailing_success(valid_success_buffer.begin(), valid_success_buffer.end());
    trailing_success.push_back(std::byte{0xEE});
    XS_CHECK(
        xs::net::DecodeHeartbeatSuccessResponse(trailing_success, &decoded_success) ==
        xs::net::HeartbeatCodecErrorCode::TrailingBytes);
}

void TestRejectsInvalidArgumentsAndSmallBuffers()
{
    const xs::net::HeartbeatRequest invalid_request{
        .sent_at_unix_ms = 1u,
        .status_flags = 1u,
        .load = MakeLoadSnapshot(0u, 0u, 0u, 0u, 0u),
    };
    std::array<std::byte, xs::net::kHeartbeatRequestSize> request_buffer{};
    XS_CHECK(
        xs::net::EncodeHeartbeatRequest(invalid_request, request_buffer) ==
        xs::net::HeartbeatCodecErrorCode::InvalidStatusFlags);

    const xs::net::HeartbeatSuccessResponse invalid_success{
        .heartbeat_interval_ms = 5000u,
        .heartbeat_timeout_ms = 5000u,
        .server_now_unix_ms = 2u,
    };
    std::array<std::byte, xs::net::kHeartbeatSuccessResponseSize> success_buffer{};
    XS_CHECK(
        xs::net::EncodeHeartbeatSuccessResponse(invalid_success, success_buffer) ==
        xs::net::HeartbeatCodecErrorCode::InvalidHeartbeatTiming);

    std::array<std::byte, xs::net::kHeartbeatRequestSize - 1> short_request_buffer{};
    const xs::net::HeartbeatRequest valid_request{
        .sent_at_unix_ms = 2u,
        .status_flags = 0u,
        .load = MakeLoadSnapshot(3u, 4u, 5u, 6u, 7u),
    };
    XS_CHECK(
        xs::net::EncodeHeartbeatRequest(valid_request, short_request_buffer) ==
        xs::net::HeartbeatCodecErrorCode::BufferTooSmall);

    XS_CHECK(
        xs::net::DecodeHeartbeatRequest(request_buffer, nullptr) ==
        xs::net::HeartbeatCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::DecodeHeartbeatSuccessResponse(success_buffer, nullptr) ==
        xs::net::HeartbeatCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::HeartbeatCodecErrorMessage(xs::net::HeartbeatCodecErrorCode::TrailingBytes) ==
        std::string_view("Heartbeat buffer must not contain trailing bytes."));
}

} // namespace

int main()
{
    TestEncodeRequestProducesExpectedWireBytesAndRoundTrip();
    TestEncodeSuccessResponseRoundTrip();
    TestRejectsSemanticViolationsAndMalformedBuffers();
    TestRejectsInvalidArgumentsAndSmallBuffers();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " heartbeat codec test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


#include "message/RegisterCodec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

[[nodiscard]] xs::net::Endpoint MakeEndpoint(std::string host, std::uint16_t port)
{
    return xs::net::Endpoint{
        .host = std::move(host),
        .port = port,
    };
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

void CheckEndpointEquals(const xs::net::Endpoint& actual, const xs::net::Endpoint& expected)
{
    XS_CHECK(actual.host == expected.host);
    XS_CHECK(actual.port == expected.port);
}

void TestGetWireSizeAndEncodeRequestRoundTrip()
{
    const xs::net::RegisterRequest request{
        .process_type = static_cast<std::uint16_t>(xs::net::ControlProcessType::Gate),
        .process_flags = 0u,
        .node_id = "Gate0",
        .pid = 0x11223344u,
        .started_at_unix_ms = 0x0102030405060708ull,
        .service_endpoint = MakeEndpoint("127.0.0.1", 6000u),
        .build_version = "build-42",
        .capability_tags = {"kcp", "relay"},
        .load = MakeLoadSnapshot(1u, 2u, 3u, 4u, 5u),
    };

    std::size_t wire_size = 0;
    XS_CHECK(xs::net::GetRegisterRequestWireSize(request, &wire_size) == xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(wire_size == 80u);

    std::array<std::byte, 80> buffer{};
    XS_CHECK(xs::net::EncodeRegisterRequest(request, buffer) == xs::net::RegisterCodecErrorCode::None);

    const std::array<std::byte, 80> expected{
        std::byte{0x00}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x00},
        std::byte{0x00}, std::byte{0x05},
        std::byte{0x47}, std::byte{0x61}, std::byte{0x74}, std::byte{0x65}, std::byte{0x30},
        std::byte{0x11}, std::byte{0x22}, std::byte{0x33}, std::byte{0x44},
        std::byte{0x01}, std::byte{0x02}, std::byte{0x03}, std::byte{0x04},
        std::byte{0x05}, std::byte{0x06}, std::byte{0x07}, std::byte{0x08},
        std::byte{0x00}, std::byte{0x09},
        std::byte{0x31}, std::byte{0x32}, std::byte{0x37}, std::byte{0x2E}, std::byte{0x30},
        std::byte{0x2E}, std::byte{0x30}, std::byte{0x2E}, std::byte{0x31},
        std::byte{0x17}, std::byte{0x70},
        std::byte{0x00}, std::byte{0x08},
        std::byte{0x62}, std::byte{0x75}, std::byte{0x69}, std::byte{0x6C},
        std::byte{0x64}, std::byte{0x2D}, std::byte{0x34}, std::byte{0x32},
        std::byte{0x00}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x03},
        std::byte{0x6B}, std::byte{0x63}, std::byte{0x70},
        std::byte{0x00}, std::byte{0x05},
        std::byte{0x72}, std::byte{0x65}, std::byte{0x6C}, std::byte{0x61}, std::byte{0x79},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x03},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x04},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x05},
    };
    XS_CHECK(ByteSpanEqualsSpan(buffer, expected));

    xs::net::RegisterRequest decoded{};
    XS_CHECK(xs::net::DecodeRegisterRequest(buffer, &decoded) == xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(decoded.process_type == request.process_type);
    XS_CHECK(decoded.process_flags == request.process_flags);
    XS_CHECK(decoded.node_id == request.node_id);
    XS_CHECK(decoded.pid == request.pid);
    XS_CHECK(decoded.started_at_unix_ms == request.started_at_unix_ms);
    CheckEndpointEquals(decoded.service_endpoint, request.service_endpoint);
    XS_CHECK(decoded.build_version == request.build_version);
    XS_CHECK(decoded.capability_tags == request.capability_tags);
    CheckLoadSnapshotEquals(decoded.load, request.load);
}

void TestEncodeSuccessAndErrorResponsesRoundTrip()
{
    const xs::net::RegisterSuccessResponse success{
        .heartbeat_interval_ms = 5000u,
        .heartbeat_timeout_ms = 15000u,
        .server_now_unix_ms = 0x2122232425262728ull,
    };

    std::array<std::byte, xs::net::kRegisterSuccessResponseSize> success_buffer{};
    XS_CHECK(
        xs::net::EncodeRegisterSuccessResponse(success, success_buffer) ==
        xs::net::RegisterCodecErrorCode::None);

    const std::array<std::byte, xs::net::kRegisterSuccessResponseSize> expected_success{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x13}, std::byte{0x88},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x3A}, std::byte{0x98},
        std::byte{0x21}, std::byte{0x22}, std::byte{0x23}, std::byte{0x24},
        std::byte{0x25}, std::byte{0x26}, std::byte{0x27}, std::byte{0x28},
    };
    XS_CHECK(ByteSpanEqualsSpan(success_buffer, expected_success));

    xs::net::RegisterSuccessResponse decoded_success{};
    XS_CHECK(
        xs::net::DecodeRegisterSuccessResponse(success_buffer, &decoded_success) ==
        xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(decoded_success.heartbeat_interval_ms == success.heartbeat_interval_ms);
    XS_CHECK(decoded_success.heartbeat_timeout_ms == success.heartbeat_timeout_ms);
    XS_CHECK(decoded_success.server_now_unix_ms == success.server_now_unix_ms);

    const xs::net::RegisterErrorResponse error{
        .error_code = 3002,
        .retry_after_ms = 500u,
    };

    std::array<std::byte, xs::net::kRegisterErrorResponseSize> error_buffer{};
    XS_CHECK(
        xs::net::EncodeRegisterErrorResponse(error, error_buffer) ==
        xs::net::RegisterCodecErrorCode::None);

    const std::array<std::byte, xs::net::kRegisterErrorResponseSize> expected_error{
        std::byte{0x00}, std::byte{0x00}, std::byte{0x0B}, std::byte{0xBA},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0xF4},
    };
    XS_CHECK(ByteSpanEqualsSpan(error_buffer, expected_error));

    xs::net::RegisterErrorResponse decoded_error{};
    XS_CHECK(
        xs::net::DecodeRegisterErrorResponse(error_buffer, &decoded_error) ==
        xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(decoded_error.error_code == error.error_code);
    XS_CHECK(decoded_error.retry_after_ms == error.retry_after_ms);
}

void TestRejectsSemanticViolationsAndMalformedBuffers()
{
    const xs::net::RegisterRequest request{
        .process_type = static_cast<std::uint16_t>(xs::net::ControlProcessType::Gate),
        .process_flags = 0u,
        .node_id = "Gate0",
        .pid = 0x11223344u,
        .started_at_unix_ms = 0x0102030405060708ull,
        .service_endpoint = MakeEndpoint("127.0.0.1", 6000u),
        .build_version = "build-42",
        .capability_tags = {"kcp", "relay"},
        .load = MakeLoadSnapshot(1u, 2u, 3u, 4u, 5u),
    };

    std::array<std::byte, 80> request_buffer{};
    XS_CHECK(xs::net::EncodeRegisterRequest(request, request_buffer) == xs::net::RegisterCodecErrorCode::None);

    auto invalid_flags = request_buffer;
    invalid_flags[3] = std::byte{0x01};
    xs::net::RegisterRequest decoded_request{};
    XS_CHECK(
        xs::net::DecodeRegisterRequest(invalid_flags, &decoded_request) ==
        xs::net::RegisterCodecErrorCode::InvalidProcessFlags);

    auto invalid_process_type = request_buffer;
    invalid_process_type[0] = std::byte{0x00};
    invalid_process_type[1] = std::byte{0x00};
    XS_CHECK(
        xs::net::DecodeRegisterRequest(invalid_process_type, &decoded_request) ==
        xs::net::RegisterCodecErrorCode::InvalidProcessType);

    auto invalid_endpoint_port = request_buffer;
    invalid_endpoint_port[34] = std::byte{0x00};
    invalid_endpoint_port[35] = std::byte{0x00};
    XS_CHECK(
        xs::net::DecodeRegisterRequest(invalid_endpoint_port, &decoded_request) ==
        xs::net::RegisterCodecErrorCode::InvalidServiceEndpointPort);

    auto invalid_tag_count = request_buffer;
    invalid_tag_count[46] = std::byte{0x00};
    invalid_tag_count[47] = std::byte{0x21};
    XS_CHECK(
        xs::net::DecodeRegisterRequest(invalid_tag_count, &decoded_request) ==
        xs::net::RegisterCodecErrorCode::TooManyCapabilityTags);

    const std::span<const std::byte> truncated_request(request_buffer.data(), request_buffer.size() - 1u);
    XS_CHECK(
        xs::net::DecodeRegisterRequest(truncated_request, &decoded_request) ==
        xs::net::RegisterCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing_request(request_buffer.begin(), request_buffer.end());
    trailing_request.push_back(std::byte{0xFF});
    XS_CHECK(
        xs::net::DecodeRegisterRequest(trailing_request, &decoded_request) ==
        xs::net::RegisterCodecErrorCode::TrailingBytes);

    const xs::net::RegisterSuccessResponse success{
        .heartbeat_interval_ms = 5000u,
        .heartbeat_timeout_ms = 15000u,
        .server_now_unix_ms = 0x2122232425262728ull,
    };
    std::array<std::byte, xs::net::kRegisterSuccessResponseSize> success_buffer{};
    XS_CHECK(
        xs::net::EncodeRegisterSuccessResponse(success, success_buffer) ==
        xs::net::RegisterCodecErrorCode::None);
    success_buffer[0] = std::byte{0x00};
    success_buffer[1] = std::byte{0x00};
    success_buffer[2] = std::byte{0x3A};
    success_buffer[3] = std::byte{0x98};

    xs::net::RegisterSuccessResponse decoded_success{};
    XS_CHECK(
        xs::net::DecodeRegisterSuccessResponse(success_buffer, &decoded_success) ==
        xs::net::RegisterCodecErrorCode::InvalidHeartbeatTiming);

    const xs::net::RegisterErrorResponse error{
        .error_code = 3002,
        .retry_after_ms = 500u,
    };
    std::array<std::byte, xs::net::kRegisterErrorResponseSize> error_buffer{};
    XS_CHECK(
        xs::net::EncodeRegisterErrorResponse(error, error_buffer) ==
        xs::net::RegisterCodecErrorCode::None);
    std::vector<std::byte> trailing_error(error_buffer.begin(), error_buffer.end());
    trailing_error.push_back(std::byte{0xFF});

    xs::net::RegisterErrorResponse decoded_error{};
    XS_CHECK(
        xs::net::DecodeRegisterErrorResponse(trailing_error, &decoded_error) ==
        xs::net::RegisterCodecErrorCode::TrailingBytes);
}

void TestRejectsInvalidArgumentsAndSizeViolations()
{
    const xs::net::RegisterRequest empty_node_id{
        .process_type = static_cast<std::uint16_t>(xs::net::ControlProcessType::Gate),
        .process_flags = 0u,
        .node_id = "",
        .pid = 1u,
        .started_at_unix_ms = 2u,
        .service_endpoint = MakeEndpoint("127.0.0.1", 6000u),
        .build_version = "build-42",
        .capability_tags = {},
        .load = MakeLoadSnapshot(0u, 0u, 0u, 0u, 0u),
    };
    std::size_t wire_size = 0;
    XS_CHECK(
        xs::net::GetRegisterRequestWireSize(empty_node_id, &wire_size) ==
        xs::net::RegisterCodecErrorCode::InvalidNodeId);

    const xs::net::RegisterRequest empty_endpoint_host{
        .process_type = static_cast<std::uint16_t>(xs::net::ControlProcessType::Gate),
        .process_flags = 0u,
        .node_id = "Gate0",
        .pid = 1u,
        .started_at_unix_ms = 2u,
        .service_endpoint = MakeEndpoint("", 6000u),
        .build_version = "build-42",
        .capability_tags = {},
        .load = MakeLoadSnapshot(0u, 0u, 0u, 0u, 0u),
    };
    XS_CHECK(
        xs::net::GetRegisterRequestWireSize(empty_endpoint_host, &wire_size) ==
        xs::net::RegisterCodecErrorCode::InvalidServiceEndpointHost);

    xs::net::RegisterRequest too_many_tags{
        .process_type = static_cast<std::uint16_t>(xs::net::ControlProcessType::Gate),
        .process_flags = 0u,
        .node_id = "Gate0",
        .pid = 1u,
        .started_at_unix_ms = 2u,
        .service_endpoint = MakeEndpoint("127.0.0.1", 6000u),
        .build_version = "build-42",
        .capability_tags = {},
        .load = MakeLoadSnapshot(0u, 0u, 0u, 0u, 0u),
    };
    too_many_tags.capability_tags.resize(xs::net::kRegisterMaxCapabilityTagCount + 1u, "tag");
    XS_CHECK(
        xs::net::GetRegisterRequestWireSize(too_many_tags, &wire_size) ==
        xs::net::RegisterCodecErrorCode::TooManyCapabilityTags);

    xs::net::RegisterRequest too_long_build_version{
        .process_type = static_cast<std::uint16_t>(xs::net::ControlProcessType::Gate),
        .process_flags = 0u,
        .node_id = "Gate0",
        .pid = 1u,
        .started_at_unix_ms = 2u,
        .service_endpoint = MakeEndpoint("127.0.0.1", 6000u),
        .build_version = std::string(static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u, 'a'),
        .capability_tags = {},
        .load = MakeLoadSnapshot(0u, 0u, 0u, 0u, 0u),
    };
    XS_CHECK(
        xs::net::GetRegisterRequestWireSize(too_long_build_version, &wire_size) ==
        xs::net::RegisterCodecErrorCode::LengthOverflow);

    const xs::net::RegisterRequest valid_request{
        .process_type = static_cast<std::uint16_t>(xs::net::ControlProcessType::Gate),
        .process_flags = 0u,
        .node_id = "Gate0",
        .pid = 0x11223344u,
        .started_at_unix_ms = 0x0102030405060708ull,
        .service_endpoint = MakeEndpoint("127.0.0.1", 6000u),
        .build_version = "build-42",
        .capability_tags = {"kcp", "relay"},
        .load = MakeLoadSnapshot(1u, 2u, 3u, 4u, 5u),
    };
    XS_CHECK(xs::net::GetRegisterRequestWireSize(valid_request, &wire_size) == xs::net::RegisterCodecErrorCode::None);
    std::vector<std::byte> short_request_buffer(wire_size - 1u);
    XS_CHECK(
        xs::net::EncodeRegisterRequest(valid_request, short_request_buffer) ==
        xs::net::RegisterCodecErrorCode::BufferTooSmall);

    const xs::net::RegisterSuccessResponse invalid_success{
        .heartbeat_interval_ms = 5000u,
        .heartbeat_timeout_ms = 5000u,
        .server_now_unix_ms = 2u,
    };
    std::array<std::byte, xs::net::kRegisterSuccessResponseSize> success_buffer{};
    XS_CHECK(
        xs::net::EncodeRegisterSuccessResponse(invalid_success, success_buffer) ==
        xs::net::RegisterCodecErrorCode::InvalidHeartbeatTiming);

    std::array<std::byte, xs::net::kRegisterErrorResponseSize> error_buffer{};
    XS_CHECK(
        xs::net::GetRegisterRequestWireSize(valid_request, nullptr) ==
        xs::net::RegisterCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::DecodeRegisterRequest(std::span<const std::byte>(short_request_buffer.data(), short_request_buffer.size()), nullptr) ==
        xs::net::RegisterCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::DecodeRegisterSuccessResponse(success_buffer, nullptr) ==
        xs::net::RegisterCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::DecodeRegisterErrorResponse(error_buffer, nullptr) ==
        xs::net::RegisterCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::RegisterCodecErrorMessage(xs::net::RegisterCodecErrorCode::TrailingBytes) ==
        std::string_view("Register buffer must not contain trailing bytes."));
}

} // namespace

int main()
{
    TestGetWireSizeAndEncodeRequestRoundTrip();
    TestEncodeSuccessAndErrorResponsesRoundTrip();
    TestRejectsSemanticViolationsAndMalformedBuffers();
    TestRejectsInvalidArgumentsAndSizeViolations();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " register codec test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


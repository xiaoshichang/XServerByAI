#include "message/RelayCodec.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <span>
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

void TestRelayForwardStubCallRoundTrip()
{
    const xs::net::RelayForwardStubCall message{
        .source_game_node_id = "Game0",
        .target_game_node_id = "Game1",
        .target_stub_type = "MatchStub",
        .stub_call_msg_id = 5101u,
        .relay_flags = 0u,
        .payload = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}},
    };

    std::size_t wire_size = 0u;
    XS_CHECK(xs::net::GetRelayForwardStubCallWireSize(message, &wire_size) == xs::net::RelayCodecErrorCode::None);

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodeRelayForwardStubCall(message, buffer) == xs::net::RelayCodecErrorCode::None);

    xs::net::RelayForwardStubCall decoded{};
    XS_CHECK(xs::net::DecodeRelayForwardStubCall(buffer, &decoded) == xs::net::RelayCodecErrorCode::None);
    XS_CHECK(decoded.source_game_node_id == message.source_game_node_id);
    XS_CHECK(decoded.target_game_node_id == message.target_game_node_id);
    XS_CHECK(decoded.target_stub_type == message.target_stub_type);
    XS_CHECK(decoded.stub_call_msg_id == message.stub_call_msg_id);
    XS_CHECK(decoded.relay_flags == message.relay_flags);
    XS_CHECK(decoded.payload == message.payload);
}

void TestRelayForwardStubCallRejectsSemanticViolations()
{
    xs::net::RelayForwardStubCall message{
        .source_game_node_id = "Game0",
        .target_game_node_id = "Game1",
        .target_stub_type = "MatchStub",
        .stub_call_msg_id = 5101u,
        .relay_flags = 0u,
        .payload = {},
    };

    std::size_t wire_size = 0u;
    message.stub_call_msg_id = 0u;
    XS_CHECK(xs::net::GetRelayForwardStubCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidMessageId);

    message.stub_call_msg_id = 5101u;
    message.source_game_node_id.clear();
    XS_CHECK(xs::net::GetRelayForwardStubCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidSourceGameNodeId);

    message.source_game_node_id = "Game0";
    message.target_game_node_id.clear();
    XS_CHECK(xs::net::GetRelayForwardStubCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidTargetGameNodeId);

    message.target_game_node_id = "Game1";
    message.target_stub_type.clear();
    XS_CHECK(xs::net::GetRelayForwardStubCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidTargetStubType);

    message.target_stub_type = "MatchStub";
    message.relay_flags = 1u;
    XS_CHECK(xs::net::GetRelayForwardStubCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidRelayFlags);
}

void TestRelayForwardStubCallRejectsMalformedBuffers()
{
    const xs::net::RelayForwardStubCall message{
        .source_game_node_id = "Game0",
        .target_game_node_id = "Game1",
        .target_stub_type = "MatchStub",
        .stub_call_msg_id = 5101u,
        .relay_flags = 0u,
        .payload = {std::byte{0x0A}, std::byte{0x0B}},
    };

    std::size_t wire_size = 0u;
    XS_CHECK(xs::net::GetRelayForwardStubCallWireSize(message, &wire_size) == xs::net::RelayCodecErrorCode::None);
    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodeRelayForwardStubCall(message, buffer) == xs::net::RelayCodecErrorCode::None);

    xs::net::RelayForwardStubCall decoded{};
    XS_CHECK(xs::net::DecodeRelayForwardStubCall(
                 std::span<const std::byte>(buffer.data(), buffer.size() - 1u),
                 &decoded) == xs::net::RelayCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing = buffer;
    trailing.push_back(std::byte{0x7F});
    XS_CHECK(xs::net::DecodeRelayForwardStubCall(trailing, &decoded) == xs::net::RelayCodecErrorCode::TrailingBytes);

    std::vector<std::byte> invalid_flags = buffer;
    const std::size_t relay_flags_offset =
        sizeof(std::uint16_t) + 5u +
        sizeof(std::uint16_t) + 5u +
        sizeof(std::uint16_t) + 9u +
        sizeof(std::uint32_t);
    invalid_flags[relay_flags_offset + 3u] = std::byte{0x01};
    XS_CHECK(xs::net::DecodeRelayForwardStubCall(invalid_flags, &decoded) ==
             xs::net::RelayCodecErrorCode::InvalidRelayFlags);
}

void TestRelayForwardProxyCallRoundTrip()
{
    const xs::net::RelayForwardProxyCall message{
        .source_game_node_id = "Game0",
        .route_gate_node_id = "Gate3",
        .target_entity_id = "01234567-89ab-cdef-0123-456789abcdef",
        .proxy_call_msg_id = 6201u,
        .relay_flags = 0u,
        .payload = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}},
    };

    std::size_t wire_size = 0u;
    XS_CHECK(xs::net::GetRelayForwardProxyCallWireSize(message, &wire_size) == xs::net::RelayCodecErrorCode::None);

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodeRelayForwardProxyCall(message, buffer) == xs::net::RelayCodecErrorCode::None);

    xs::net::RelayForwardProxyCall decoded{};
    XS_CHECK(xs::net::DecodeRelayForwardProxyCall(buffer, &decoded) == xs::net::RelayCodecErrorCode::None);
    XS_CHECK(decoded.source_game_node_id == message.source_game_node_id);
    XS_CHECK(decoded.route_gate_node_id == message.route_gate_node_id);
    XS_CHECK(decoded.target_entity_id == message.target_entity_id);
    XS_CHECK(decoded.proxy_call_msg_id == message.proxy_call_msg_id);
    XS_CHECK(decoded.relay_flags == message.relay_flags);
    XS_CHECK(decoded.payload == message.payload);
}

void TestRelayForwardProxyCallRejectsSemanticViolations()
{
    xs::net::RelayForwardProxyCall message{
        .source_game_node_id = "Game0",
        .route_gate_node_id = "Gate3",
        .target_entity_id = "01234567-89ab-cdef-0123-456789abcdef",
        .proxy_call_msg_id = 6201u,
        .relay_flags = 0u,
        .payload = {},
    };

    std::size_t wire_size = 0u;
    message.proxy_call_msg_id = 0u;
    XS_CHECK(xs::net::GetRelayForwardProxyCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidMessageId);

    message.proxy_call_msg_id = 6201u;
    message.source_game_node_id.clear();
    XS_CHECK(xs::net::GetRelayForwardProxyCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidSourceGameNodeId);

    message.source_game_node_id = "Game0";
    message.route_gate_node_id.clear();
    XS_CHECK(xs::net::GetRelayForwardProxyCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidRouteGateNodeId);

    message.route_gate_node_id = "Gate3";
    message.target_entity_id = "not-a-guid";
    XS_CHECK(xs::net::GetRelayForwardProxyCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidTargetEntityId);

    message.target_entity_id = "01234567-89ab-cdef-0123-456789abcdef";
    message.relay_flags = 1u;
    XS_CHECK(xs::net::GetRelayForwardProxyCallWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidRelayFlags);
}

void TestRelayForwardProxyCallRejectsMalformedBuffers()
{
    const xs::net::RelayForwardProxyCall message{
        .source_game_node_id = "Game0",
        .route_gate_node_id = "Gate3",
        .target_entity_id = "01234567-89ab-cdef-0123-456789abcdef",
        .proxy_call_msg_id = 6201u,
        .relay_flags = 0u,
        .payload = {std::byte{0x5A}, std::byte{0x5B}},
    };

    std::size_t wire_size = 0u;
    XS_CHECK(xs::net::GetRelayForwardProxyCallWireSize(message, &wire_size) == xs::net::RelayCodecErrorCode::None);
    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodeRelayForwardProxyCall(message, buffer) == xs::net::RelayCodecErrorCode::None);

    xs::net::RelayForwardProxyCall decoded{};
    XS_CHECK(xs::net::DecodeRelayForwardProxyCall(
                 std::span<const std::byte>(buffer.data(), buffer.size() - 1u),
                 &decoded) == xs::net::RelayCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing = buffer;
    trailing.push_back(std::byte{0x7F});
    XS_CHECK(xs::net::DecodeRelayForwardProxyCall(trailing, &decoded) == xs::net::RelayCodecErrorCode::TrailingBytes);

    std::vector<std::byte> invalid_flags = buffer;
    const std::size_t relay_flags_offset =
        sizeof(std::uint16_t) + 5u +
        sizeof(std::uint16_t) + 5u +
        sizeof(std::uint16_t) + 36u +
        sizeof(std::uint32_t);
    invalid_flags[relay_flags_offset + 3u] = std::byte{0x01};
    XS_CHECK(xs::net::DecodeRelayForwardProxyCall(invalid_flags, &decoded) ==
             xs::net::RelayCodecErrorCode::InvalidRelayFlags);
}

void TestRelayPushToClientRoundTrip()
{
    const xs::net::RelayPushToClient message{
        .source_game_node_id = "Game0",
        .route_gate_node_id = "Gate3",
        .target_entity_id = "01234567-89ab-cdef-0123-456789abcdef",
        .client_msg_id = 6201u,
        .relay_flags = 0u,
        .payload = {std::byte{0x44}, std::byte{0x55}, std::byte{0x66}},
    };

    std::size_t wire_size = 0u;
    XS_CHECK(xs::net::GetRelayPushToClientWireSize(message, &wire_size) == xs::net::RelayCodecErrorCode::None);

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodeRelayPushToClient(message, buffer) == xs::net::RelayCodecErrorCode::None);

    xs::net::RelayPushToClient decoded{};
    XS_CHECK(xs::net::DecodeRelayPushToClient(buffer, &decoded) == xs::net::RelayCodecErrorCode::None);
    XS_CHECK(decoded.source_game_node_id == message.source_game_node_id);
    XS_CHECK(decoded.route_gate_node_id == message.route_gate_node_id);
    XS_CHECK(decoded.target_entity_id == message.target_entity_id);
    XS_CHECK(decoded.client_msg_id == message.client_msg_id);
    XS_CHECK(decoded.relay_flags == message.relay_flags);
    XS_CHECK(decoded.payload == message.payload);
}

void TestRelayPushToClientRejectsSemanticViolations()
{
    xs::net::RelayPushToClient message{
        .source_game_node_id = "Game0",
        .route_gate_node_id = "Gate3",
        .target_entity_id = "01234567-89ab-cdef-0123-456789abcdef",
        .client_msg_id = 6201u,
        .relay_flags = 0u,
        .payload = {},
    };

    std::size_t wire_size = 0u;
    message.client_msg_id = 0u;
    XS_CHECK(xs::net::GetRelayPushToClientWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidMessageId);

    message.client_msg_id = 6201u;
    message.source_game_node_id.clear();
    XS_CHECK(xs::net::GetRelayPushToClientWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidSourceGameNodeId);

    message.source_game_node_id = "Game0";
    message.route_gate_node_id.clear();
    XS_CHECK(xs::net::GetRelayPushToClientWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidRouteGateNodeId);

    message.route_gate_node_id = "Gate3";
    message.target_entity_id = "not-a-guid";
    XS_CHECK(xs::net::GetRelayPushToClientWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidTargetEntityId);

    message.target_entity_id = "01234567-89ab-cdef-0123-456789abcdef";
    message.relay_flags = 1u;
    XS_CHECK(xs::net::GetRelayPushToClientWireSize(message, &wire_size) ==
             xs::net::RelayCodecErrorCode::InvalidRelayFlags);
}

void TestRelayPushToClientRejectsMalformedBuffers()
{
    const xs::net::RelayPushToClient message{
        .source_game_node_id = "Game0",
        .route_gate_node_id = "Gate3",
        .target_entity_id = "01234567-89ab-cdef-0123-456789abcdef",
        .client_msg_id = 6201u,
        .relay_flags = 0u,
        .payload = {std::byte{0x7A}, std::byte{0x7B}},
    };

    std::size_t wire_size = 0u;
    XS_CHECK(xs::net::GetRelayPushToClientWireSize(message, &wire_size) == xs::net::RelayCodecErrorCode::None);
    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodeRelayPushToClient(message, buffer) == xs::net::RelayCodecErrorCode::None);

    xs::net::RelayPushToClient decoded{};
    XS_CHECK(xs::net::DecodeRelayPushToClient(
                 std::span<const std::byte>(buffer.data(), buffer.size() - 1u),
                 &decoded) == xs::net::RelayCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing = buffer;
    trailing.push_back(std::byte{0x7F});
    XS_CHECK(xs::net::DecodeRelayPushToClient(trailing, &decoded) == xs::net::RelayCodecErrorCode::TrailingBytes);

    std::vector<std::byte> invalid_flags = buffer;
    const std::size_t relay_flags_offset =
        sizeof(std::uint16_t) + 5u +
        sizeof(std::uint16_t) + 5u +
        sizeof(std::uint16_t) + 36u +
        sizeof(std::uint32_t);
    invalid_flags[relay_flags_offset + 3u] = std::byte{0x01};
    XS_CHECK(xs::net::DecodeRelayPushToClient(invalid_flags, &decoded) ==
             xs::net::RelayCodecErrorCode::InvalidRelayFlags);
}

} // namespace

int main()
{
    TestRelayForwardStubCallRoundTrip();
    TestRelayForwardStubCallRejectsSemanticViolations();
    TestRelayForwardStubCallRejectsMalformedBuffers();
    TestRelayForwardProxyCallRoundTrip();
    TestRelayForwardProxyCallRejectsSemanticViolations();
    TestRelayForwardProxyCallRejectsMalformedBuffers();
    TestRelayPushToClientRoundTrip();
    TestRelayPushToClientRejectsSemanticViolations();
    TestRelayPushToClientRejectsMalformedBuffers();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " relay codec test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

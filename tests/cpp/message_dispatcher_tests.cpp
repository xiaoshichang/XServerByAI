#include "MessageDispatcher.h"

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

[[nodiscard]] std::vector<std::byte> EncodeTextPacket(
    std::uint32_t msg_id,
    std::uint32_t seq,
    std::uint16_t flags,
    std::string_view payload_text)
{
    const auto payload = BytesFromText(payload_text);
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(msg_id, seq, flags, static_cast<std::uint32_t>(payload.size()));

    std::size_t wire_size = 0;
    XS_CHECK(xs::net::GetPacketWireSize(payload.size(), &wire_size) == xs::net::PacketCodecErrorCode::None);

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(xs::net::EncodePacket(header, payload, buffer) == xs::net::PacketCodecErrorCode::None);
    return buffer;
}

void TestDispatchInvokesExactHandler()
{
    xs::net::MessageDispatcher dispatcher;
    const auto expected_payload = BytesFromText("hello");
    const auto buffer = EncodeTextPacket(
        2000u,
        42u,
        static_cast<std::uint16_t>(xs::net::PacketFlag::Response),
        "hello");

    xs::net::PacketView packet{};
    XS_CHECK(xs::net::DecodePacket(buffer, &packet) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(packet.payload.data() == buffer.data() + xs::net::kPacketHeaderSize);

    bool invoked = false;
    std::uint32_t seen_seq = 0u;
    std::span<const std::byte> seen_payload{};
    std::uint16_t seen_flags = 0u;

    XS_CHECK(
        dispatcher.RegisterHandler(2000u, [&](const xs::net::PacketView& routed_packet) {
            invoked = true;
            seen_seq = routed_packet.header.seq;
            seen_flags = routed_packet.header.flags;
            seen_payload = routed_packet.payload;
            XS_CHECK(routed_packet.header.msg_id == 2000u);
        }) == xs::net::MessageDispatchErrorCode::None);
    XS_CHECK(dispatcher.HasHandler(2000u));
    XS_CHECK(dispatcher.handler_count() == 1u);

    XS_CHECK(dispatcher.Dispatch(packet) == xs::net::MessageDispatchErrorCode::None);
    XS_CHECK(invoked);
    XS_CHECK(seen_seq == 42u);
    XS_CHECK(seen_flags == static_cast<std::uint16_t>(xs::net::PacketFlag::Response));
    XS_CHECK(seen_payload.data() == packet.payload.data());
    XS_CHECK(ByteSpanEqualsSpan(seen_payload, expected_payload));
}

void TestRegisterRejectsInvalidInputs()
{
    xs::net::MessageDispatcher dispatcher;

    XS_CHECK(
        dispatcher.RegisterHandler(0u, [](const xs::net::PacketView&) {
        }) == xs::net::MessageDispatchErrorCode::InvalidMessageId);
    XS_CHECK(
        dispatcher.RegisterHandler(1000u, xs::net::MessageHandler{}) ==
        xs::net::MessageDispatchErrorCode::HandlerEmpty);
    XS_CHECK(
        dispatcher.RegisterHandler(1000u, [](const xs::net::PacketView&) {
        }) == xs::net::MessageDispatchErrorCode::None);
    XS_CHECK(
        dispatcher.RegisterHandler(1000u, [](const xs::net::PacketView&) {
        }) == xs::net::MessageDispatchErrorCode::HandlerAlreadyRegistered);
    XS_CHECK(dispatcher.handler_count() == 1u);
    XS_CHECK(
        xs::net::MessageDispatchErrorMessage(xs::net::MessageDispatchErrorCode::HandlerAlreadyRegistered) ==
        std::string_view("Message handler already registered for msgId."));
}

void TestDispatchRejectsInvalidOrUnknownMessages()
{
    xs::net::MessageDispatcher dispatcher;

    const auto invalid_buffer = EncodeTextPacket(0u, 1u, 0u, "bad");
    xs::net::PacketView invalid_packet{};
    XS_CHECK(xs::net::DecodePacket(invalid_buffer, &invalid_packet) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(dispatcher.Dispatch(invalid_packet) == xs::net::MessageDispatchErrorCode::InvalidMessageId);
    XS_CHECK(
        xs::net::MessageDispatchErrorMessage(xs::net::MessageDispatchErrorCode::InvalidMessageId) ==
        std::string_view("Message msgId must not be zero."));

    const auto unknown_buffer = EncodeTextPacket(2001u, 3u, 0u, "unknown");
    xs::net::PacketView unknown_packet{};
    XS_CHECK(xs::net::DecodePacket(unknown_buffer, &unknown_packet) == xs::net::PacketCodecErrorCode::None);
    XS_CHECK(dispatcher.Dispatch(unknown_packet) == xs::net::MessageDispatchErrorCode::HandlerNotFound);
}

void TestUnregisterAndClearRemoveHandlers()
{
    xs::net::MessageDispatcher dispatcher;
    int first_hits = 0;
    int second_hits = 0;

    XS_CHECK(
        dispatcher.RegisterHandler(1000u, [&](const xs::net::PacketView&) {
            ++first_hits;
        }) == xs::net::MessageDispatchErrorCode::None);
    XS_CHECK(
        dispatcher.RegisterHandler(1100u, [&](const xs::net::PacketView&) {
            ++second_hits;
        }) == xs::net::MessageDispatchErrorCode::None);
    XS_CHECK(dispatcher.handler_count() == 2u);

    const auto first_buffer = EncodeTextPacket(1000u, 1u, 0u, "first");
    xs::net::PacketView first_packet{};
    XS_CHECK(xs::net::DecodePacket(first_buffer, &first_packet) == xs::net::PacketCodecErrorCode::None);

    const auto second_buffer = EncodeTextPacket(1100u, 2u, 0u, "second");
    xs::net::PacketView second_packet{};
    XS_CHECK(xs::net::DecodePacket(second_buffer, &second_packet) == xs::net::PacketCodecErrorCode::None);

    XS_CHECK(dispatcher.UnregisterHandler(1000u) == xs::net::MessageDispatchErrorCode::None);
    XS_CHECK(!dispatcher.HasHandler(1000u));
    XS_CHECK(dispatcher.Dispatch(first_packet) == xs::net::MessageDispatchErrorCode::HandlerNotFound);
    XS_CHECK(first_hits == 0);

    XS_CHECK(dispatcher.UnregisterHandler(1000u) == xs::net::MessageDispatchErrorCode::HandlerNotFound);
    XS_CHECK(dispatcher.UnregisterHandler(0u) == xs::net::MessageDispatchErrorCode::InvalidMessageId);

    XS_CHECK(dispatcher.Dispatch(second_packet) == xs::net::MessageDispatchErrorCode::None);
    XS_CHECK(second_hits == 1);

    dispatcher.Clear();
    XS_CHECK(dispatcher.handler_count() == 0u);
    XS_CHECK(!dispatcher.HasHandler(1100u));
    XS_CHECK(dispatcher.Dispatch(second_packet) == xs::net::MessageDispatchErrorCode::HandlerNotFound);
    XS_CHECK(second_hits == 1);
}

} // namespace

int main()
{
    TestDispatchInvokesExactHandler();
    TestRegisterRejectsInvalidInputs();
    TestDispatchRejectsInvalidOrUnknownMessages();
    TestUnregisterAndClearRemoveHandlers();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " message dispatcher test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
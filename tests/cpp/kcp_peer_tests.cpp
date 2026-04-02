#include "KcpPeer.h"

#include <cstddef>
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

std::vector<std::byte> BytesFromText(std::string_view value)
{
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());

    for (const char character : value)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }

    return bytes;
}

bool ByteSpanEqualsSpan(std::span<const std::byte> left, std::span<const std::byte> right)
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

void TestKcpPeerAppliesConfigAndExposesRuntimeState()
{
    xs::core::KcpConfig config;
    config.mtu = 1000U;
    config.sndwnd = 64U;
    config.rcvwnd = 160U;
    config.nodelay = false;
    config.interval_ms = 20U;
    config.fast_resend = 1U;
    config.no_congestion_window = true;
    config.min_rto_ms = 40U;
    config.dead_link_count = 30U;
    config.stream_mode = true;

    xs::net::KcpPeer peer({
        .conversation = 42U,
        .config = config,
    });
    XS_CHECK_MSG(peer.valid(), peer.last_error_message().data());

    const xs::net::KcpPeerRuntimeState runtime_state = peer.runtime_state();
    XS_CHECK(runtime_state.conversation == 42U);
    XS_CHECK(runtime_state.mtu == config.mtu);
    XS_CHECK(runtime_state.sndwnd == config.sndwnd);
    XS_CHECK(runtime_state.rcvwnd == config.rcvwnd);
    XS_CHECK(runtime_state.nodelay == config.nodelay);
    XS_CHECK(runtime_state.interval_ms == config.interval_ms);
    XS_CHECK(runtime_state.fast_resend == config.fast_resend);
    XS_CHECK(runtime_state.no_congestion_window == config.no_congestion_window);
    XS_CHECK(runtime_state.min_rto_ms == config.min_rto_ms);
    XS_CHECK(runtime_state.dead_link_count == config.dead_link_count);
    XS_CHECK(runtime_state.stream_mode == config.stream_mode);
    XS_CHECK(peer.pending_datagram_count() == 0U);
    XS_CHECK(peer.pending_datagram_bytes() == 0U);
    XS_CHECK(peer.peek_next_message_size() == 0U);
}

void TestKcpPeerTransfersMessageBetweenPeers()
{
    const auto payload = BytesFromText("hello-kcp");
    xs::core::KcpConfig config;
    config.no_congestion_window = true;

    xs::net::KcpPeer sender({
        .conversation = 7U,
        .config = config,
    });
    xs::net::KcpPeer receiver({
        .conversation = 7U,
        .config = config,
    });
    XS_CHECK_MSG(sender.valid(), sender.last_error_message().data());
    XS_CHECK_MSG(receiver.valid(), receiver.last_error_message().data());

    XS_CHECK(sender.Send(payload) == xs::net::KcpPeerErrorCode::None);
    XS_CHECK(sender.Flush(0U) == xs::net::KcpPeerErrorCode::None);

    std::vector<std::vector<std::byte>> datagrams = sender.ConsumeOutgoingDatagrams();
    XS_CHECK(!datagrams.empty());
    for (const auto& datagram : datagrams)
    {
        XS_CHECK(receiver.Input(datagram) == xs::net::KcpPeerErrorCode::None);
    }

    std::vector<std::byte> received;
    XS_CHECK(receiver.Receive(&received) == xs::net::KcpPeerErrorCode::None);
    XS_CHECK(ByteSpanEqualsSpan(received, payload));
}

void TestKcpPeerRejectsInvalidOperations()
{
    xs::core::KcpConfig invalid_config;
    invalid_config.mtu = 0U;

    xs::net::KcpPeer invalid_peer({
        .conversation = 1U,
        .config = invalid_config,
    });
    XS_CHECK(!invalid_peer.valid());
    XS_CHECK(std::string(invalid_peer.last_error_message()).find("mtu") != std::string::npos);

    xs::net::KcpPeer peer({
        .conversation = 1U,
        .config = {},
    });
    XS_CHECK_MSG(peer.valid(), peer.last_error_message().data());

    const std::vector<std::byte> empty_payload;
    const std::span<const std::byte> empty_datagram;
    std::vector<std::byte> output;
    XS_CHECK(peer.Send(empty_payload) == xs::net::KcpPeerErrorCode::InvalidArgument);
    XS_CHECK(peer.Input(empty_datagram) == xs::net::KcpPeerErrorCode::InvalidArgument);
    XS_CHECK(peer.Receive(&output) == xs::net::KcpPeerErrorCode::MessageUnavailable);
}

} // namespace

int main()
{
    TestKcpPeerAppliesConfigAndExposesRuntimeState();
    TestKcpPeerTransfersMessageBetweenPeers();
    TestKcpPeerRejectsInvalidOperations();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " KCP peer test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include "message/InnerClusterCodec.h"

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

void TestEncodeClusterReadyNotifyRoundTrip()
{
    const xs::net::ClusterReadyNotify notify{
        .ready_epoch = 0x2122232425262728ull,
        .cluster_ready = true,
        .status_flags = 0u,
        .server_now_unix_ms = 0x3132333435363738ull,
    };

    std::array<std::byte, xs::net::kClusterReadyNotifySize> buffer{};
    XS_CHECK(
        xs::net::EncodeClusterReadyNotify(notify, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    const std::array<std::byte, xs::net::kClusterReadyNotifySize> expected{
        std::byte{0x21}, std::byte{0x22}, std::byte{0x23}, std::byte{0x24},
        std::byte{0x25}, std::byte{0x26}, std::byte{0x27}, std::byte{0x28},
        std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x31}, std::byte{0x32}, std::byte{0x33}, std::byte{0x34},
        std::byte{0x35}, std::byte{0x36}, std::byte{0x37}, std::byte{0x38},
    };
    XS_CHECK(ByteSpanEqualsSpan(buffer, expected));

    xs::net::ClusterReadyNotify decoded{};
    XS_CHECK(
        xs::net::DecodeClusterReadyNotify(buffer, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(decoded.ready_epoch == notify.ready_epoch);
    XS_CHECK(decoded.cluster_ready == notify.cluster_ready);
    XS_CHECK(decoded.status_flags == notify.status_flags);
    XS_CHECK(decoded.server_now_unix_ms == notify.server_now_unix_ms);
}

void TestEncodeClusterNodesOnlineNotifyRoundTrip()
{
    const xs::net::ClusterNodesOnlineNotify notify{
        .all_nodes_online = true,
        .status_flags = 0u,
        .server_now_unix_ms = 0x4142434445464748ull,
    };

    std::array<std::byte, xs::net::kClusterNodesOnlineNotifySize> buffer{};
    XS_CHECK(
        xs::net::EncodeClusterNodesOnlineNotify(notify, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    const std::array<std::byte, xs::net::kClusterNodesOnlineNotifySize> expected{
        std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x41}, std::byte{0x42}, std::byte{0x43}, std::byte{0x44},
        std::byte{0x45}, std::byte{0x46}, std::byte{0x47}, std::byte{0x48},
    };
    XS_CHECK(ByteSpanEqualsSpan(buffer, expected));

    xs::net::ClusterNodesOnlineNotify decoded{};
    XS_CHECK(
        xs::net::DecodeClusterNodesOnlineNotify(buffer, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(decoded.all_nodes_online == notify.all_nodes_online);
    XS_CHECK(decoded.status_flags == notify.status_flags);
    XS_CHECK(decoded.server_now_unix_ms == notify.server_now_unix_ms);
}

void TestRejectsClusterNodesOnlineSemanticViolationsAndMalformedBuffers()
{
    const xs::net::ClusterNodesOnlineNotify valid_notify{
        .all_nodes_online = false,
        .status_flags = 0u,
        .server_now_unix_ms = 9u,
    };

    std::array<std::byte, xs::net::kClusterNodesOnlineNotifySize> buffer{};
    XS_CHECK(
        xs::net::EncodeClusterNodesOnlineNotify(valid_notify, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    auto invalid_status_flags = buffer;
    invalid_status_flags[4] = std::byte{0x01};
    xs::net::ClusterNodesOnlineNotify decoded{};
    XS_CHECK(
        xs::net::DecodeClusterNodesOnlineNotify(invalid_status_flags, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidNodesOnlineStatusFlags);

    auto invalid_bool = buffer;
    invalid_bool[0] = std::byte{0x02};
    XS_CHECK(
        xs::net::DecodeClusterNodesOnlineNotify(invalid_bool, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidBoolValue);

    const std::span<const std::byte> truncated(buffer.data(), buffer.size() - 1u);
    XS_CHECK(
        xs::net::DecodeClusterNodesOnlineNotify(truncated, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing(buffer.begin(), buffer.end());
    trailing.push_back(std::byte{0xBB});
    XS_CHECK(
        xs::net::DecodeClusterNodesOnlineNotify(trailing, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::TrailingBytes);
}

void TestRejectsClusterReadySemanticViolationsAndMalformedBuffers()
{
    const xs::net::ClusterReadyNotify valid_notify{
        .ready_epoch = 3u,
        .cluster_ready = false,
        .status_flags = 0u,
        .server_now_unix_ms = 4u,
    };

    std::array<std::byte, xs::net::kClusterReadyNotifySize> buffer{};
    XS_CHECK(
        xs::net::EncodeClusterReadyNotify(valid_notify, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    auto invalid_status_flags = buffer;
    invalid_status_flags[12] = std::byte{0x01};
    xs::net::ClusterReadyNotify decoded{};
    XS_CHECK(
        xs::net::DecodeClusterReadyNotify(invalid_status_flags, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidReadyStatusFlags);

    auto invalid_bool = buffer;
    invalid_bool[8] = std::byte{0x02};
    XS_CHECK(
        xs::net::DecodeClusterReadyNotify(invalid_bool, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidBoolValue);

    const std::span<const std::byte> truncated(buffer.data(), buffer.size() - 1u);
    XS_CHECK(
        xs::net::DecodeClusterReadyNotify(truncated, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing(buffer.begin(), buffer.end());
    trailing.push_back(std::byte{0xAA});
    XS_CHECK(
        xs::net::DecodeClusterReadyNotify(trailing, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::TrailingBytes);
}

void TestRejectsInvalidArgumentsAndSizeViolations()
{
    const xs::net::ClusterNodesOnlineNotify invalid_nodes_online{
        .all_nodes_online = true,
        .status_flags = 1u,
        .server_now_unix_ms = 2u,
    };
    std::array<std::byte, xs::net::kClusterNodesOnlineNotifySize> nodes_online_buffer{};
    XS_CHECK(
        xs::net::EncodeClusterNodesOnlineNotify(invalid_nodes_online, nodes_online_buffer) ==
        xs::net::InnerClusterCodecErrorCode::InvalidNodesOnlineStatusFlags);

    std::array<std::byte, xs::net::kClusterNodesOnlineNotifySize - 1u> short_nodes_online_buffer{};
    const xs::net::ClusterNodesOnlineNotify valid_nodes_online{
        .all_nodes_online = false,
        .status_flags = 0u,
        .server_now_unix_ms = 3u,
    };
    XS_CHECK(
        xs::net::EncodeClusterNodesOnlineNotify(valid_nodes_online, short_nodes_online_buffer) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);
    XS_CHECK(
        xs::net::DecodeClusterNodesOnlineNotify(nodes_online_buffer, nullptr) ==
        xs::net::InnerClusterCodecErrorCode::InvalidArgument);

    const xs::net::ClusterReadyNotify invalid_ready{
        .ready_epoch = 1u,
        .cluster_ready = true,
        .status_flags = 1u,
        .server_now_unix_ms = 2u,
    };
    std::array<std::byte, xs::net::kClusterReadyNotifySize> ready_buffer{};
    XS_CHECK(
        xs::net::EncodeClusterReadyNotify(invalid_ready, ready_buffer) ==
        xs::net::InnerClusterCodecErrorCode::InvalidReadyStatusFlags);

    std::array<std::byte, xs::net::kClusterReadyNotifySize - 1u> short_ready_buffer{};
    const xs::net::ClusterReadyNotify valid_ready{
        .ready_epoch = 2u,
        .cluster_ready = false,
        .status_flags = 0u,
        .server_now_unix_ms = 3u,
    };
    XS_CHECK(
        xs::net::EncodeClusterReadyNotify(valid_ready, short_ready_buffer) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);

    XS_CHECK(
        xs::net::DecodeClusterReadyNotify(ready_buffer, nullptr) ==
        xs::net::InnerClusterCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::InnerClusterCodecErrorMessage(xs::net::InnerClusterCodecErrorCode::TrailingBytes) ==
        std::string_view("Inner-cluster buffer must not contain trailing bytes."));
    XS_CHECK(
        xs::net::InnerClusterCodecErrorMessage(xs::net::InnerClusterCodecErrorCode::InvalidNodesOnlineStatusFlags) ==
        std::string_view("ClusterNodesOnlineNotify statusFlags must be zero."));
}

} // namespace

int main()
{
    TestEncodeClusterNodesOnlineNotifyRoundTrip();
    TestRejectsClusterNodesOnlineSemanticViolationsAndMalformedBuffers();
    TestEncodeClusterReadyNotifyRoundTrip();
    TestRejectsClusterReadySemanticViolationsAndMalformedBuffers();
    TestRejectsInvalidArgumentsAndSizeViolations();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " inner-cluster codec test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

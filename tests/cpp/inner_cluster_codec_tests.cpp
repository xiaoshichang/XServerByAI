#include "message/InnerClusterCodec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
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

void TestEncodeGameGateMeshReadyReportRoundTrip()
{
    const xs::net::GameGateMeshReadyReport report{
        .mesh_ready = true,
        .status_flags = 0u,
        .reported_at_unix_ms = 0x5152535455565758ull,
    };

    std::array<std::byte, xs::net::kGameGateMeshReadyReportSize> buffer{};
    XS_CHECK(
        xs::net::EncodeGameGateMeshReadyReport(report, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    const std::array<std::byte, xs::net::kGameGateMeshReadyReportSize> expected{
        std::byte{0x01},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x51}, std::byte{0x52}, std::byte{0x53}, std::byte{0x54},
        std::byte{0x55}, std::byte{0x56}, std::byte{0x57}, std::byte{0x58},
    };
    XS_CHECK(ByteSpanEqualsSpan(buffer, expected));

    xs::net::GameGateMeshReadyReport decoded{};
    XS_CHECK(
        xs::net::DecodeGameGateMeshReadyReport(buffer, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(decoded.mesh_ready == report.mesh_ready);
    XS_CHECK(decoded.status_flags == report.status_flags);
    XS_CHECK(decoded.reported_at_unix_ms == report.reported_at_unix_ms);
}

void TestEncodeServerStubOwnershipSyncRoundTrip()
{
    const xs::net::ServerStubOwnershipSync sync{
        .assignment_epoch = 7u,
        .status_flags = 0u,
        .assignments =
            {
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .owner_game_node_id = "Game0",
                    .entry_flags = 0u,
                },
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "ChatService",
                    .entity_key = "default",
                    .owner_game_node_id = "Game1",
                    .entry_flags = 0u,
                },
            },
        .server_now_unix_ms = 11u,
    };

    std::size_t wire_size = 0u;
    XS_CHECK(
        xs::net::GetServerStubOwnershipSyncWireSize(sync, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(wire_size > sizeof(std::uint64_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t));

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(
        xs::net::EncodeServerStubOwnershipSync(sync, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    xs::net::ServerStubOwnershipSync decoded{};
    XS_CHECK(
        xs::net::DecodeServerStubOwnershipSync(buffer, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(decoded.assignment_epoch == sync.assignment_epoch);
    XS_CHECK(decoded.status_flags == sync.status_flags);
    XS_CHECK(decoded.server_now_unix_ms == sync.server_now_unix_ms);
    XS_CHECK(decoded.assignments.size() == sync.assignments.size());
    XS_CHECK(decoded.assignments[0].entity_type == "MatchService");
    XS_CHECK(decoded.assignments[0].entity_key == "default");
    XS_CHECK(decoded.assignments[0].owner_game_node_id == "Game0");
    XS_CHECK(decoded.assignments[0].entry_flags == 0u);
    XS_CHECK(decoded.assignments[1].entity_type == "ChatService");
    XS_CHECK(decoded.assignments[1].entity_key == "default");
    XS_CHECK(decoded.assignments[1].owner_game_node_id == "Game1");
    XS_CHECK(decoded.assignments[1].entry_flags == 0u);
}

void TestEncodeGameServiceReadyReportRoundTrip()
{
    const xs::net::GameServiceReadyReport report{
        .assignment_epoch = 13u,
        .local_ready = true,
        .status_flags = 0u,
        .entries =
            {
                xs::net::ServerStubReadyEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .ready = true,
                    .entry_flags = 0u,
                },
                xs::net::ServerStubReadyEntry{
                    .entity_type = "LeaderboardService",
                    .entity_key = "default",
                    .ready = true,
                    .entry_flags = 0u,
                },
            },
        .reported_at_unix_ms = 17u,
    };

    std::size_t wire_size = 0u;
    XS_CHECK(
        xs::net::GetGameServiceReadyReportWireSize(report, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(wire_size > sizeof(std::uint64_t) + sizeof(std::uint8_t) + sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t));

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(
        xs::net::EncodeGameServiceReadyReport(report, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    xs::net::GameServiceReadyReport decoded{};
    XS_CHECK(
        xs::net::DecodeGameServiceReadyReport(buffer, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(decoded.assignment_epoch == report.assignment_epoch);
    XS_CHECK(decoded.local_ready == report.local_ready);
    XS_CHECK(decoded.status_flags == report.status_flags);
    XS_CHECK(decoded.reported_at_unix_ms == report.reported_at_unix_ms);
    XS_CHECK(decoded.entries.size() == report.entries.size());
    XS_CHECK(decoded.entries[0].entity_type == "MatchService");
    XS_CHECK(decoded.entries[0].entity_key == "default");
    XS_CHECK(decoded.entries[0].ready);
    XS_CHECK(decoded.entries[0].entry_flags == 0u);
    XS_CHECK(decoded.entries[1].entity_type == "LeaderboardService");
    XS_CHECK(decoded.entries[1].entity_key == "default");
    XS_CHECK(decoded.entries[1].ready);
    XS_CHECK(decoded.entries[1].entry_flags == 0u);
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

void TestRejectsGameGateMeshReadySemanticViolationsAndMalformedBuffers()
{
    const xs::net::GameGateMeshReadyReport valid_report{
        .mesh_ready = false,
        .status_flags = 0u,
        .reported_at_unix_ms = 5u,
    };

    std::array<std::byte, xs::net::kGameGateMeshReadyReportSize> buffer{};
    XS_CHECK(
        xs::net::EncodeGameGateMeshReadyReport(valid_report, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    auto invalid_status_flags = buffer;
    invalid_status_flags[4] = std::byte{0x01};
    xs::net::GameGateMeshReadyReport decoded{};
    XS_CHECK(
        xs::net::DecodeGameGateMeshReadyReport(invalid_status_flags, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidMeshReadyStatusFlags);

    auto invalid_bool = buffer;
    invalid_bool[0] = std::byte{0x02};
    XS_CHECK(
        xs::net::DecodeGameGateMeshReadyReport(invalid_bool, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidBoolValue);

    const std::span<const std::byte> truncated(buffer.data(), buffer.size() - 1u);
    XS_CHECK(
        xs::net::DecodeGameGateMeshReadyReport(truncated, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing(buffer.begin(), buffer.end());
    trailing.push_back(std::byte{0xCC});
    XS_CHECK(
        xs::net::DecodeGameGateMeshReadyReport(trailing, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::TrailingBytes);
}

void TestRejectsServerStubOwnershipSyncSemanticViolationsAndMalformedBuffers()
{
    const xs::net::ServerStubOwnershipSync valid_sync{
        .assignment_epoch = 9u,
        .status_flags = 0u,
        .assignments =
            {
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "LeaderboardService",
                    .entity_key = "default",
                    .owner_game_node_id = "Game0",
                    .entry_flags = 0u,
                },
            },
        .server_now_unix_ms = 10u,
    };

    std::size_t wire_size = 0u;
    XS_CHECK(
        xs::net::GetServerStubOwnershipSyncWireSize(valid_sync, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(
        xs::net::EncodeServerStubOwnershipSync(valid_sync, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    auto invalid_status_flags = buffer;
    invalid_status_flags[11] = std::byte{0x01};
    xs::net::ServerStubOwnershipSync decoded{};
    XS_CHECK(
        xs::net::DecodeServerStubOwnershipSync(invalid_status_flags, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidOwnershipStatusFlags);

    std::vector<std::byte> invalid_entry_flags = buffer;
    invalid_entry_flags[invalid_entry_flags.size() - sizeof(std::uint64_t) - 1u] = std::byte{0x01};
    XS_CHECK(
        xs::net::DecodeServerStubOwnershipSync(invalid_entry_flags, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidOwnershipEntryFlags);

    const std::span<const std::byte> truncated(buffer.data(), buffer.size() - 1u);
    XS_CHECK(
        xs::net::DecodeServerStubOwnershipSync(truncated, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing = buffer;
    trailing.push_back(std::byte{0xDD});
    XS_CHECK(
        xs::net::DecodeServerStubOwnershipSync(trailing, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::TrailingBytes);
}

void TestRejectsGameServiceReadyReportSemanticViolationsAndMalformedBuffers()
{
    const xs::net::GameServiceReadyReport valid_report{
        .assignment_epoch = 21u,
        .local_ready = true,
        .status_flags = 0u,
        .entries =
            {
                xs::net::ServerStubReadyEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .ready = true,
                    .entry_flags = 0u,
                },
            },
        .reported_at_unix_ms = 22u,
    };

    std::size_t wire_size = 0u;
    XS_CHECK(
        xs::net::GetGameServiceReadyReportWireSize(valid_report, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> buffer(wire_size);
    XS_CHECK(
        xs::net::EncodeGameServiceReadyReport(valid_report, buffer) ==
        xs::net::InnerClusterCodecErrorCode::None);

    const std::size_t status_flags_offset = sizeof(std::uint64_t) + sizeof(std::uint8_t);
    std::vector<std::byte> invalid_status_flags = buffer;
    invalid_status_flags[status_flags_offset + sizeof(std::uint32_t) - 1u] = std::byte{0x01};
    xs::net::GameServiceReadyReport decoded{};
    XS_CHECK(
        xs::net::DecodeGameServiceReadyReport(invalid_status_flags, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidServiceReadyStatusFlags);

    const std::size_t entry_ready_offset =
        sizeof(std::uint64_t) +
        sizeof(std::uint8_t) +
        sizeof(std::uint32_t) +
        sizeof(std::uint32_t) +
        sizeof(std::uint16_t) + valid_report.entries[0].entity_type.size() +
        sizeof(std::uint16_t) + valid_report.entries[0].entity_key.size();

    std::vector<std::byte> invalid_entry_ready = buffer;
    invalid_entry_ready[entry_ready_offset] = std::byte{0x02};
    XS_CHECK(
        xs::net::DecodeGameServiceReadyReport(invalid_entry_ready, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidBoolValue);

    std::vector<std::byte> invalid_entry_flags = buffer;
    invalid_entry_flags[invalid_entry_flags.size() - sizeof(std::uint64_t) - 1u] = std::byte{0x01};
    XS_CHECK(
        xs::net::DecodeGameServiceReadyReport(invalid_entry_flags, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidServiceReadyEntryFlags);

    std::vector<std::byte> invalid_local_ready = buffer;
    invalid_local_ready[sizeof(std::uint64_t)] = std::byte{0x02};
    XS_CHECK(
        xs::net::DecodeGameServiceReadyReport(invalid_local_ready, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::InvalidBoolValue);

    const std::span<const std::byte> truncated(buffer.data(), buffer.size() - 1u);
    XS_CHECK(
        xs::net::DecodeGameServiceReadyReport(truncated, &decoded) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);

    std::vector<std::byte> trailing = buffer;
    trailing.push_back(std::byte{0xEE});
    XS_CHECK(
        xs::net::DecodeGameServiceReadyReport(trailing, &decoded) ==
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

    const xs::net::GameGateMeshReadyReport invalid_mesh_ready{
        .mesh_ready = true,
        .status_flags = 1u,
        .reported_at_unix_ms = 4u,
    };
    std::array<std::byte, xs::net::kGameGateMeshReadyReportSize> mesh_ready_buffer{};
    XS_CHECK(
        xs::net::EncodeGameGateMeshReadyReport(invalid_mesh_ready, mesh_ready_buffer) ==
        xs::net::InnerClusterCodecErrorCode::InvalidMeshReadyStatusFlags);

    std::array<std::byte, xs::net::kGameGateMeshReadyReportSize - 1u> short_mesh_ready_buffer{};
    const xs::net::GameGateMeshReadyReport valid_mesh_ready{
        .mesh_ready = false,
        .status_flags = 0u,
        .reported_at_unix_ms = 5u,
    };
    XS_CHECK(
        xs::net::EncodeGameGateMeshReadyReport(valid_mesh_ready, short_mesh_ready_buffer) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);
    XS_CHECK(
        xs::net::DecodeGameGateMeshReadyReport(mesh_ready_buffer, nullptr) ==
        xs::net::InnerClusterCodecErrorCode::InvalidArgument);

    const xs::net::GameServiceReadyReport invalid_service_ready{
        .assignment_epoch = 1u,
        .local_ready = true,
        .status_flags = 1u,
        .entries =
            {
                xs::net::ServerStubReadyEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .ready = true,
                    .entry_flags = 0u,
                },
            },
        .reported_at_unix_ms = 2u,
    };
    std::size_t wire_size = 0u;
    XS_CHECK(
        xs::net::GetGameServiceReadyReportWireSize(invalid_service_ready, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::InvalidServiceReadyStatusFlags);

    const xs::net::GameServiceReadyReport invalid_service_ready_entry{
        .assignment_epoch = 1u,
        .local_ready = true,
        .status_flags = 0u,
        .entries =
            {
                xs::net::ServerStubReadyEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .ready = true,
                    .entry_flags = 1u,
                },
            },
        .reported_at_unix_ms = 2u,
    };
    XS_CHECK(
        xs::net::GetGameServiceReadyReportWireSize(invalid_service_ready_entry, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::InvalidServiceReadyEntryFlags);

    const xs::net::GameServiceReadyReport valid_service_ready{
        .assignment_epoch = 1u,
        .local_ready = true,
        .status_flags = 0u,
        .entries =
            {
                xs::net::ServerStubReadyEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .ready = true,
                    .entry_flags = 0u,
                },
            },
        .reported_at_unix_ms = 2u,
    };
    XS_CHECK(
        xs::net::GetGameServiceReadyReportWireSize(valid_service_ready, nullptr) ==
        xs::net::InnerClusterCodecErrorCode::InvalidArgument);
    XS_CHECK(
        xs::net::DecodeGameServiceReadyReport(std::span<const std::byte>{}, nullptr) ==
        xs::net::InnerClusterCodecErrorCode::InvalidArgument);

    std::size_t valid_service_ready_wire_size = 0u;
    XS_CHECK(
        xs::net::GetGameServiceReadyReportWireSize(valid_service_ready, &valid_service_ready_wire_size) ==
        xs::net::InnerClusterCodecErrorCode::None);
    std::vector<std::byte> short_service_ready_buffer(valid_service_ready_wire_size - 1u);
    XS_CHECK(
        xs::net::EncodeGameServiceReadyReport(valid_service_ready, short_service_ready_buffer) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);

    const xs::net::GameServiceReadyReport overflow_service_ready{
        .assignment_epoch = 1u,
        .local_ready = true,
        .status_flags = 0u,
        .entries =
            {
                xs::net::ServerStubReadyEntry{
                    .entity_type = std::string(static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u, 'A'),
                    .entity_key = "default",
                    .ready = true,
                    .entry_flags = 0u,
                },
            },
        .reported_at_unix_ms = 2u,
    };
    XS_CHECK(
        xs::net::GetGameServiceReadyReportWireSize(overflow_service_ready, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::LengthOverflow);

    const xs::net::ServerStubOwnershipSync invalid_sync{
        .assignment_epoch = 1u,
        .status_flags = 1u,
        .assignments =
            {
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .owner_game_node_id = "Game0",
                    .entry_flags = 0u,
                },
            },
        .server_now_unix_ms = 2u,
    };
    XS_CHECK(
        xs::net::GetServerStubOwnershipSyncWireSize(invalid_sync, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::InvalidOwnershipStatusFlags);

    const xs::net::ServerStubOwnershipSync invalid_entry_sync{
        .assignment_epoch = 1u,
        .status_flags = 0u,
        .assignments =
            {
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .owner_game_node_id = "Game0",
                    .entry_flags = 1u,
                },
            },
        .server_now_unix_ms = 2u,
    };
    XS_CHECK(
        xs::net::GetServerStubOwnershipSyncWireSize(invalid_entry_sync, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::InvalidOwnershipEntryFlags);

    const xs::net::ServerStubOwnershipSync valid_sync{
        .assignment_epoch = 1u,
        .status_flags = 0u,
        .assignments =
            {
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "MatchService",
                    .entity_key = "default",
                    .owner_game_node_id = "Game0",
                    .entry_flags = 0u,
                },
            },
        .server_now_unix_ms = 2u,
    };
    XS_CHECK(
        xs::net::GetServerStubOwnershipSyncWireSize(valid_sync, nullptr) ==
        xs::net::InnerClusterCodecErrorCode::InvalidArgument);

    XS_CHECK(
        xs::net::DecodeServerStubOwnershipSync(std::span<const std::byte>{}, nullptr) ==
        xs::net::InnerClusterCodecErrorCode::InvalidArgument);

    std::size_t valid_wire_size = 0u;
    XS_CHECK(
        xs::net::GetServerStubOwnershipSyncWireSize(valid_sync, &valid_wire_size) ==
        xs::net::InnerClusterCodecErrorCode::None);
    std::vector<std::byte> short_sync_buffer(valid_wire_size - 1u);
    XS_CHECK(
        xs::net::EncodeServerStubOwnershipSync(valid_sync, short_sync_buffer) ==
        xs::net::InnerClusterCodecErrorCode::BufferTooSmall);

    const xs::net::ServerStubOwnershipSync overflow_sync{
        .assignment_epoch = 1u,
        .status_flags = 0u,
        .assignments =
            {
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = std::string(static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u, 'A'),
                    .entity_key = "default",
                    .owner_game_node_id = "Game0",
                    .entry_flags = 0u,
                },
            },
        .server_now_unix_ms = 2u,
    };
    XS_CHECK(
        xs::net::GetServerStubOwnershipSyncWireSize(overflow_sync, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::LengthOverflow);

    XS_CHECK(
        xs::net::InnerClusterCodecErrorMessage(xs::net::InnerClusterCodecErrorCode::TrailingBytes) ==
        std::string_view("Inner-cluster buffer must not contain trailing bytes."));
    XS_CHECK(
        xs::net::InnerClusterCodecErrorMessage(xs::net::InnerClusterCodecErrorCode::InvalidNodesOnlineStatusFlags) ==
        std::string_view("ClusterNodesOnlineNotify statusFlags must be zero."));
    XS_CHECK(
        xs::net::InnerClusterCodecErrorMessage(xs::net::InnerClusterCodecErrorCode::InvalidMeshReadyStatusFlags) ==
        std::string_view("GameGateMeshReadyReport statusFlags must be zero."));
    XS_CHECK(
        xs::net::InnerClusterCodecErrorMessage(xs::net::InnerClusterCodecErrorCode::InvalidOwnershipEntryFlags) ==
        std::string_view("ServerStubOwnershipEntry entryFlags must be zero."));
    XS_CHECK(
        xs::net::InnerClusterCodecErrorMessage(xs::net::InnerClusterCodecErrorCode::InvalidServiceReadyStatusFlags) ==
        std::string_view("GameServiceReadyReport statusFlags must be zero."));
    XS_CHECK(
        xs::net::InnerClusterCodecErrorMessage(xs::net::InnerClusterCodecErrorCode::InvalidServiceReadyEntryFlags) ==
        std::string_view("ServerStubReadyEntry entryFlags must be zero."));
}

} // namespace

int main()
{
    TestEncodeClusterNodesOnlineNotifyRoundTrip();
    TestRejectsClusterNodesOnlineSemanticViolationsAndMalformedBuffers();
    TestEncodeGameGateMeshReadyReportRoundTrip();
    TestRejectsGameGateMeshReadySemanticViolationsAndMalformedBuffers();
    TestEncodeServerStubOwnershipSyncRoundTrip();
    TestEncodeGameServiceReadyReportRoundTrip();
    TestRejectsServerStubOwnershipSyncSemanticViolationsAndMalformedBuffers();
    TestRejectsGameServiceReadyReportSemanticViolationsAndMalformedBuffers();
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

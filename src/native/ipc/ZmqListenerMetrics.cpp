#include "ZmqListenerMetrics.h"

#include <chrono>

namespace xs::ipc
{
namespace
{

[[nodiscard]] std::int64_t CurrentUnixTimeMilliseconds() noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

void ZmqListenerMetrics::Reset() noexcept
{
    active_connection_count_.store(0, std::memory_order_relaxed);
    received_message_count_.store(0, std::memory_order_relaxed);
    received_payload_bytes_.store(0, std::memory_order_relaxed);
    sent_message_count_.store(0, std::memory_order_relaxed);
    sent_payload_bytes_.store(0, std::memory_order_relaxed);
    snapshot_unix_ms_.store(0, std::memory_order_relaxed);
}

void ZmqListenerMetrics::SetActiveConnectionCount(std::uint64_t connection_count) noexcept
{
    const std::uint64_t previous_count = active_connection_count_.load(std::memory_order_relaxed);
    if (previous_count == connection_count)
    {
        return;
    }

    active_connection_count_.store(connection_count, std::memory_order_relaxed);
    TouchSnapshotTime();
}

void ZmqListenerMetrics::RecordReceivedMessage(std::size_t payload_size) noexcept
{
    received_message_count_.fetch_add(1u, std::memory_order_relaxed);
    received_payload_bytes_.fetch_add(static_cast<std::uint64_t>(payload_size), std::memory_order_relaxed);
    TouchSnapshotTime();
}

void ZmqListenerMetrics::RecordSentMessage(std::size_t payload_size) noexcept
{
    sent_message_count_.fetch_add(1u, std::memory_order_relaxed);
    sent_payload_bytes_.fetch_add(static_cast<std::uint64_t>(payload_size), std::memory_order_relaxed);
    TouchSnapshotTime();
}

ZmqListenerMetricsSnapshot ZmqListenerMetrics::Snapshot() const noexcept
{
    return ZmqListenerMetricsSnapshot{
        .active_connection_count = active_connection_count_.load(std::memory_order_relaxed),
        .received_message_count = received_message_count_.load(std::memory_order_relaxed),
        .received_payload_bytes = received_payload_bytes_.load(std::memory_order_relaxed),
        .sent_message_count = sent_message_count_.load(std::memory_order_relaxed),
        .sent_payload_bytes = sent_payload_bytes_.load(std::memory_order_relaxed),
        .snapshot_unix_ms = snapshot_unix_ms_.load(std::memory_order_relaxed),
    };
}

void ZmqListenerMetrics::TouchSnapshotTime() noexcept
{
    snapshot_unix_ms_.store(CurrentUnixTimeMilliseconds(), std::memory_order_relaxed);
}

} // namespace xs::ipc

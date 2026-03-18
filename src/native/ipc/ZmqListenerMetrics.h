#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace xs::ipc
{

struct ZmqListenerMetricsSnapshot
{
    std::uint64_t active_connection_count{0};
    std::uint64_t received_message_count{0};
    std::uint64_t received_payload_bytes{0};
    std::uint64_t sent_message_count{0};
    std::uint64_t sent_payload_bytes{0};
    std::int64_t snapshot_unix_ms{0};
};

class ZmqListenerMetrics final
{
  public:
    ZmqListenerMetrics() noexcept = default;

    void Reset() noexcept;
    void SetActiveConnectionCount(std::uint64_t connection_count) noexcept;
    void RecordReceivedMessage(std::size_t payload_size) noexcept;
    void RecordSentMessage(std::size_t payload_size) noexcept;

    [[nodiscard]] ZmqListenerMetricsSnapshot Snapshot() const noexcept;

  private:
    void TouchSnapshotTime() noexcept;

    std::atomic_uint64_t active_connection_count_{0};
    std::atomic_uint64_t received_message_count_{0};
    std::atomic_uint64_t received_payload_bytes_{0};
    std::atomic_uint64_t sent_message_count_{0};
    std::atomic_uint64_t sent_payload_bytes_{0};
    std::atomic_int64_t snapshot_unix_ms_{0};
};

} // namespace xs::ipc

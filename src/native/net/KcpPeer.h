#pragma once

#include "Config.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace xs::net
{

enum class KcpPeerErrorCode : std::uint8_t
{
    None = 0,
    InvalidArgument,
    InvalidState,
    CreateFailed,
    ConfigInvalid,
    LengthOverflow,
    SendFailed,
    InputFailed,
    ReceiveFailed,
    MessageUnavailable,
};

[[nodiscard]] std::string_view KcpPeerErrorMessage(KcpPeerErrorCode error_code) noexcept;

struct KcpPeerOptions
{
    std::uint32_t conversation{0U};
    xs::core::KcpConfig config{};
};

struct KcpPeerRuntimeState
{
    std::uint32_t conversation{0U};
    std::uint32_t mtu{0U};
    std::uint32_t sndwnd{0U};
    std::uint32_t rcvwnd{0U};
    bool nodelay{false};
    std::uint32_t interval_ms{0U};
    std::uint32_t fast_resend{0U};
    bool no_congestion_window{false};
    std::uint32_t min_rto_ms{0U};
    std::uint32_t dead_link_count{0U};
    bool stream_mode{false};
};

class KcpPeer final
{
  public:
    explicit KcpPeer(KcpPeerOptions options = {});
    ~KcpPeer();

    KcpPeer(const KcpPeer&) = delete;
    KcpPeer& operator=(const KcpPeer&) = delete;
    KcpPeer(KcpPeer&&) = delete;
    KcpPeer& operator=(KcpPeer&&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint32_t conversation() const noexcept;
    [[nodiscard]] const xs::core::KcpConfig& config() const noexcept;
    [[nodiscard]] KcpPeerRuntimeState runtime_state() const noexcept;
    [[nodiscard]] std::size_t pending_datagram_count() const noexcept;
    [[nodiscard]] std::size_t pending_datagram_bytes() const noexcept;
    [[nodiscard]] std::size_t peek_next_message_size() const noexcept;
    [[nodiscard]] std::uint32_t NextUpdateClock(std::uint32_t now_ms) const noexcept;
    [[nodiscard]] KcpPeerErrorCode Send(std::span<const std::byte> payload);
    [[nodiscard]] KcpPeerErrorCode Input(std::span<const std::byte> datagram);
    [[nodiscard]] KcpPeerErrorCode Update(std::uint32_t now_ms);
    [[nodiscard]] KcpPeerErrorCode Flush(std::uint32_t now_ms);
    [[nodiscard]] KcpPeerErrorCode Receive(std::vector<std::byte>* payload);
    [[nodiscard]] std::vector<std::vector<std::byte>> ConsumeOutgoingDatagrams();
    [[nodiscard]] std::string_view last_error_message() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::net

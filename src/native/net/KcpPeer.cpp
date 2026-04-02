#include "KcpPeer.h"

#include <ikcp.h>

#include <limits>
#include <string>
#include <utility>

namespace xs::net
{
namespace
{

[[nodiscard]] KcpPeerErrorCode SetError(
    std::string& last_error_message,
    KcpPeerErrorCode code,
    std::string message)
{
    if (message.empty())
    {
        last_error_message = std::string(KcpPeerErrorMessage(code));
    }
    else
    {
        last_error_message = std::move(message);
    }

    return code;
}

void ClearError(std::string& last_error_message) noexcept
{
    last_error_message.clear();
}

[[nodiscard]] bool FitsInSignedInt(std::uint32_t value) noexcept
{
    return value <= static_cast<std::uint32_t>(std::numeric_limits<int>::max());
}

} // namespace

std::string_view KcpPeerErrorMessage(KcpPeerErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case KcpPeerErrorCode::None:
        return "Success.";
    case KcpPeerErrorCode::InvalidArgument:
        return "KCP peer argument is invalid.";
    case KcpPeerErrorCode::InvalidState:
        return "KCP peer is not initialized.";
    case KcpPeerErrorCode::CreateFailed:
        return "Failed to create KCP control block.";
    case KcpPeerErrorCode::ConfigInvalid:
        return "KCP peer configuration is invalid.";
    case KcpPeerErrorCode::LengthOverflow:
        return "KCP peer payload length exceeds the supported range.";
    case KcpPeerErrorCode::SendFailed:
        return "Failed to queue data into the KCP peer send buffer.";
    case KcpPeerErrorCode::InputFailed:
        return "Failed to feed an incoming datagram into the KCP peer.";
    case KcpPeerErrorCode::ReceiveFailed:
        return "Failed to receive a decoded message from the KCP peer.";
    case KcpPeerErrorCode::MessageUnavailable:
        return "No completed KCP message is currently available.";
    }

    return "Unknown KCP peer error.";
}

class KcpPeer::Impl final
{
  public:
    explicit Impl(KcpPeerOptions options)
        : options_(std::move(options))
    {
        Initialize();
    }

    ~Impl()
    {
        if (handle_ != nullptr)
        {
            ikcp_release(handle_);
            handle_ = nullptr;
        }
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return handle_ != nullptr && last_error_message_.empty();
    }

    [[nodiscard]] std::uint32_t conversation() const noexcept
    {
        return options_.conversation;
    }

    [[nodiscard]] const xs::core::KcpConfig& config() const noexcept
    {
        return options_.config;
    }

    [[nodiscard]] KcpPeerRuntimeState runtime_state() const noexcept
    {
        if (handle_ == nullptr)
        {
            return {};
        }

        return KcpPeerRuntimeState{
            .conversation = handle_->conv,
            .mtu = static_cast<std::uint32_t>(handle_->mtu),
            .sndwnd = static_cast<std::uint32_t>(handle_->snd_wnd),
            .rcvwnd = static_cast<std::uint32_t>(handle_->rcv_wnd),
            .nodelay = handle_->nodelay != 0,
            .interval_ms = static_cast<std::uint32_t>(handle_->interval),
            .fast_resend = static_cast<std::uint32_t>(handle_->fastresend),
            .no_congestion_window = handle_->nocwnd != 0,
            .min_rto_ms = static_cast<std::uint32_t>(handle_->rx_minrto),
            .dead_link_count = handle_->dead_link,
            .stream_mode = handle_->stream != 0,
        };
    }

    [[nodiscard]] std::size_t pending_datagram_count() const noexcept
    {
        return pending_datagrams_.size();
    }

    [[nodiscard]] std::size_t pending_datagram_bytes() const noexcept
    {
        return pending_datagram_bytes_;
    }

    [[nodiscard]] std::size_t peek_next_message_size() const noexcept
    {
        if (handle_ == nullptr)
        {
            return 0U;
        }

        const int next_size = ikcp_peeksize(handle_);
        return next_size > 0 ? static_cast<std::size_t>(next_size) : 0U;
    }

    [[nodiscard]] std::uint32_t NextUpdateClock(std::uint32_t now_ms) const noexcept
    {
        return handle_ != nullptr ? ikcp_check(handle_, now_ms) : now_ms;
    }

    [[nodiscard]] KcpPeerErrorCode Send(std::span<const std::byte> payload)
    {
        if (handle_ == nullptr)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidState, {});
        }

        if (payload.empty())
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidArgument, "KCP send payload must not be empty.");
        }

        if (payload.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        {
            return SetError(last_error_message_, KcpPeerErrorCode::LengthOverflow, {});
        }

        const int result = ikcp_send(
            handle_,
            reinterpret_cast<const char*>(payload.data()),
            static_cast<int>(payload.size()));
        if (result < 0)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::SendFailed, {});
        }

        ClearError(last_error_message_);
        return KcpPeerErrorCode::None;
    }

    [[nodiscard]] KcpPeerErrorCode Input(std::span<const std::byte> datagram)
    {
        if (handle_ == nullptr)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidState, {});
        }

        if (datagram.empty())
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidArgument, "KCP input datagram must not be empty.");
        }

        if (datagram.size() > static_cast<std::size_t>(std::numeric_limits<long>::max()))
        {
            return SetError(last_error_message_, KcpPeerErrorCode::LengthOverflow, {});
        }

        const int result = ikcp_input(
            handle_,
            reinterpret_cast<const char*>(datagram.data()),
            static_cast<long>(datagram.size()));
        if (result < 0)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InputFailed, {});
        }

        ClearError(last_error_message_);
        return KcpPeerErrorCode::None;
    }

    [[nodiscard]] KcpPeerErrorCode Update(std::uint32_t now_ms)
    {
        if (handle_ == nullptr)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidState, {});
        }

        ikcp_update(handle_, now_ms);
        ClearError(last_error_message_);
        return KcpPeerErrorCode::None;
    }

    [[nodiscard]] KcpPeerErrorCode Flush(std::uint32_t now_ms)
    {
        if (handle_ == nullptr)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidState, {});
        }

        ikcp_update(handle_, now_ms);
        ClearError(last_error_message_);
        return KcpPeerErrorCode::None;
    }

    [[nodiscard]] KcpPeerErrorCode Receive(std::vector<std::byte>* payload)
    {
        if (handle_ == nullptr)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidState, {});
        }

        if (payload == nullptr)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidArgument, "KCP receive output must not be null.");
        }

        payload->clear();
        const int message_size = ikcp_peeksize(handle_);
        if (message_size < 0)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::MessageUnavailable, {});
        }

        payload->resize(static_cast<std::size_t>(message_size));
        const int result = ikcp_recv(
            handle_,
            reinterpret_cast<char*>(payload->data()),
            message_size);
        if (result < 0)
        {
            payload->clear();
            return SetError(last_error_message_, KcpPeerErrorCode::ReceiveFailed, {});
        }

        payload->resize(static_cast<std::size_t>(result));
        ClearError(last_error_message_);
        return KcpPeerErrorCode::None;
    }

    [[nodiscard]] std::vector<std::vector<std::byte>> ConsumeOutgoingDatagrams()
    {
        pending_datagram_bytes_ = 0U;
        return std::exchange(pending_datagrams_, {});
    }

    [[nodiscard]] std::string_view last_error_message() const noexcept
    {
        return last_error_message_;
    }

  private:
    void Initialize()
    {
        const KcpPeerErrorCode validation_result = ValidateOptions();
        if (validation_result != KcpPeerErrorCode::None)
        {
            return;
        }

        handle_ = ikcp_create(options_.conversation, this);
        if (handle_ == nullptr)
        {
            (void)SetError(last_error_message_, KcpPeerErrorCode::CreateFailed, {});
            return;
        }

        handle_->output = &Impl::HandleOutput;
        const KcpPeerErrorCode config_result = ApplyConfig();
        if (config_result != KcpPeerErrorCode::None)
        {
            ikcp_release(handle_);
            handle_ = nullptr;
            return;
        }

        ClearError(last_error_message_);
    }

    [[nodiscard]] KcpPeerErrorCode ValidateOptions()
    {
        const auto fail_positive = [this](std::string_view field_name) {
            return SetError(
                last_error_message_,
                KcpPeerErrorCode::ConfigInvalid,
                "KCP config." + std::string(field_name) + " must be greater than zero.");
        };

        if (options_.config.mtu == 0U)
        {
            return fail_positive("mtu");
        }

        if (options_.config.sndwnd == 0U)
        {
            return fail_positive("sndwnd");
        }

        if (options_.config.rcvwnd == 0U)
        {
            return fail_positive("rcvwnd");
        }

        if (options_.config.interval_ms == 0U)
        {
            return fail_positive("interval_ms");
        }

        if (options_.config.min_rto_ms == 0U)
        {
            return fail_positive("min_rto_ms");
        }

        if (options_.config.dead_link_count == 0U)
        {
            return fail_positive("dead_link_count");
        }

        if (!FitsInSignedInt(options_.config.mtu) ||
            !FitsInSignedInt(options_.config.sndwnd) ||
            !FitsInSignedInt(options_.config.rcvwnd) ||
            !FitsInSignedInt(options_.config.interval_ms) ||
            !FitsInSignedInt(options_.config.fast_resend) ||
            !FitsInSignedInt(options_.config.min_rto_ms))
        {
            return SetError(
                last_error_message_,
                KcpPeerErrorCode::ConfigInvalid,
                "KCP config values must fit within the upstream signed integer limits.");
        }

        return KcpPeerErrorCode::None;
    }

    [[nodiscard]] KcpPeerErrorCode ApplyConfig()
    {
        if (handle_ == nullptr)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::InvalidState, {});
        }

        if (ikcp_setmtu(handle_, static_cast<int>(options_.config.mtu)) != 0)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::ConfigInvalid, "Failed to apply KCP mtu.");
        }

        if (ikcp_wndsize(
                handle_,
                static_cast<int>(options_.config.sndwnd),
                static_cast<int>(options_.config.rcvwnd)) != 0)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::ConfigInvalid, "Failed to apply KCP window size.");
        }

        if (ikcp_nodelay(
                handle_,
                options_.config.nodelay ? 1 : 0,
                static_cast<int>(options_.config.interval_ms),
                static_cast<int>(options_.config.fast_resend),
                options_.config.no_congestion_window ? 1 : 0) != 0)
        {
            return SetError(last_error_message_, KcpPeerErrorCode::ConfigInvalid, "Failed to apply KCP nodelay settings.");
        }

        handle_->rx_minrto = static_cast<IINT32>(options_.config.min_rto_ms);
        handle_->dead_link = options_.config.dead_link_count;
        handle_->stream = options_.config.stream_mode ? 1 : 0;
        return KcpPeerErrorCode::None;
    }

    static int HandleOutput(const char* buffer, int length, ikcpcb*, void* user)
    {
        if (user == nullptr || buffer == nullptr || length < 0)
        {
            return -1;
        }

        auto* self = static_cast<Impl*>(user);
        const auto* bytes = reinterpret_cast<const std::byte*>(buffer);
        self->pending_datagrams_.emplace_back(bytes, bytes + length);
        self->pending_datagram_bytes_ += static_cast<std::size_t>(length);
        return 0;
    }

    KcpPeerOptions options_{};
    ikcpcb* handle_{nullptr};
    std::vector<std::vector<std::byte>> pending_datagrams_{};
    std::size_t pending_datagram_bytes_{0U};
    std::string last_error_message_{};
};

KcpPeer::KcpPeer(KcpPeerOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

KcpPeer::~KcpPeer() = default;

bool KcpPeer::valid() const noexcept
{
    return impl_ != nullptr && impl_->valid();
}

std::uint32_t KcpPeer::conversation() const noexcept
{
    return impl_ != nullptr ? impl_->conversation() : 0U;
}

const xs::core::KcpConfig& KcpPeer::config() const noexcept
{
    return impl_->config();
}

KcpPeerRuntimeState KcpPeer::runtime_state() const noexcept
{
    return impl_ != nullptr ? impl_->runtime_state() : KcpPeerRuntimeState{};
}

std::size_t KcpPeer::pending_datagram_count() const noexcept
{
    return impl_ != nullptr ? impl_->pending_datagram_count() : 0U;
}

std::size_t KcpPeer::pending_datagram_bytes() const noexcept
{
    return impl_ != nullptr ? impl_->pending_datagram_bytes() : 0U;
}

std::size_t KcpPeer::peek_next_message_size() const noexcept
{
    return impl_ != nullptr ? impl_->peek_next_message_size() : 0U;
}

std::uint32_t KcpPeer::NextUpdateClock(std::uint32_t now_ms) const noexcept
{
    return impl_ != nullptr ? impl_->NextUpdateClock(now_ms) : now_ms;
}

KcpPeerErrorCode KcpPeer::Send(std::span<const std::byte> payload)
{
    return impl_ != nullptr ? impl_->Send(payload) : KcpPeerErrorCode::InvalidState;
}

KcpPeerErrorCode KcpPeer::Input(std::span<const std::byte> datagram)
{
    return impl_ != nullptr ? impl_->Input(datagram) : KcpPeerErrorCode::InvalidState;
}

KcpPeerErrorCode KcpPeer::Update(std::uint32_t now_ms)
{
    return impl_ != nullptr ? impl_->Update(now_ms) : KcpPeerErrorCode::InvalidState;
}

KcpPeerErrorCode KcpPeer::Flush(std::uint32_t now_ms)
{
    return impl_ != nullptr ? impl_->Flush(now_ms) : KcpPeerErrorCode::InvalidState;
}

KcpPeerErrorCode KcpPeer::Receive(std::vector<std::byte>* payload)
{
    return impl_ != nullptr ? impl_->Receive(payload) : KcpPeerErrorCode::InvalidState;
}

std::vector<std::vector<std::byte>> KcpPeer::ConsumeOutgoingDatagrams()
{
    return impl_ != nullptr ? impl_->ConsumeOutgoingDatagrams() : std::vector<std::vector<std::byte>>{};
}

std::string_view KcpPeer::last_error_message() const noexcept
{
    return impl_ != nullptr ? impl_->last_error_message() : std::string_view{};
}

} // namespace xs::net

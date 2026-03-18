#include "ZmqPassiveListener.h"

#include <asio/error.hpp>
#include <asio/steady_timer.hpp>

#include <zmq.h>

#include <atomic>
#include <cstring>
#include <unordered_set>
#include <utility>

namespace xs::ipc
{
namespace
{

constexpr std::size_t kMonitorEventFrameSize = sizeof(std::uint16_t) + sizeof(std::uint32_t);
constexpr int kMonitorEvents = ZMQ_EVENT_ACCEPTED | ZMQ_EVENT_DISCONNECTED | ZMQ_EVENT_MONITOR_STOPPED;

std::atomic_uint64_t g_monitor_sequence{0};

[[nodiscard]] std::string BuildZmqErrorMessage(std::string_view prefix, int error_code)
{
    std::string message(prefix);
    message += ": ";
    message += zmq_strerror(error_code);
    return message;
}

[[nodiscard]] std::string MakeMonitorEndpoint()
{
    const std::uint64_t sequence = g_monitor_sequence.fetch_add(1, std::memory_order_relaxed) + 1u;
    return "inproc://xs-ipc-passive-monitor-" + std::to_string(sequence);
}

bool SetIntegerSocketOption(void* socket, int option, int value, std::string* error_message)
{
    if (zmq_setsockopt(socket, option, &value, sizeof(value)) == 0)
    {
        return true;
    }

    if (error_message != nullptr)
    {
        *error_message = BuildZmqErrorMessage("Failed to configure ZeroMQ socket option", zmq_errno());
    }
    return false;
}

bool QueryLastEndpoint(void* socket, std::string* endpoint, std::string* error_message)
{
    if (socket == nullptr || endpoint == nullptr)
    {
        if (error_message != nullptr)
        {
            *error_message = "ZeroMQ last endpoint query requires valid output storage.";
        }
        return false;
    }

    char buffer[256]{};
    size_t length = sizeof(buffer);
    if (zmq_getsockopt(socket, ZMQ_LAST_ENDPOINT, buffer, &length) == 0)
    {
        *endpoint = buffer;
        return true;
    }

    if (error_message != nullptr)
    {
        *error_message = BuildZmqErrorMessage("Failed to query ZeroMQ last endpoint", zmq_errno());
    }
    return false;
}

} // namespace

std::string_view ZmqListenerStateName(ZmqListenerState state) noexcept
{
    switch (state)
    {
    case ZmqListenerState::Stopped:
        return "Stopped";
    case ZmqListenerState::Listening:
        return "Listening";
    }

    return "Unknown";
}

class ZmqPassiveListener::Impl final : public std::enable_shared_from_this<Impl>
{
  public:
    Impl(asio::io_context& io_context, ZmqContext& context, ZmqPassiveListenerOptions options)
        : context_(context), options_(std::move(options)), pump_timer_(io_context)
    {
    }

    [[nodiscard]] ZmqSocketErrorCode Start(std::string* error_message)
    {
        if (running_)
        {
            return SetLastError(
                ZmqSocketErrorCode::AlreadyRunning,
                "ZeroMQ passive listener is already running.",
                error_message);
        }

        const ZmqSocketErrorCode validation_result = ValidateOptions(error_message);
        if (validation_result != ZmqSocketErrorCode::None)
        {
            return validation_result;
        }

        const ZmqSocketErrorCode open_result = OpenSockets(error_message);
        if (open_result != ZmqSocketErrorCode::None)
        {
            CloseSockets();
            return open_result;
        }

        active_connection_ids_.clear();
        metrics_.Reset();
        running_ = true;
        ClearLastError();
        UpdateState(ZmqListenerState::Listening);
        SchedulePump();
        return ZmqSocketErrorCode::None;
    }

    void Stop() noexcept
    {
        if (running_)
        {
            running_ = false;
            (void)pump_timer_.cancel();
        }

        CloseSockets();
        active_connection_ids_.clear();
        ClearLastError();
        UpdateState(ZmqListenerState::Stopped);
    }

    [[nodiscard]] ZmqSocketErrorCode Send(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> message,
        std::string* error_message)
    {
        if (!running_ || socket_ == nullptr)
        {
            return SetLastError(
                ZmqSocketErrorCode::NotStarted,
                "ZeroMQ passive listener must be started before Send().",
                error_message);
        }

        if (routing_id.empty())
        {
            return SetLastError(
                ZmqSocketErrorCode::EmptyRoutingId,
                "ZeroMQ passive listener routing_id must not be empty.",
                error_message);
        }

        const void* routing_data = static_cast<const void*>(routing_id.data());
        if (zmq_send(socket_, routing_data, routing_id.size(), ZMQ_DONTWAIT | ZMQ_SNDMORE) < 0)
        {
            return SetLastError(
                ZmqSocketErrorCode::RoutingIdSendFailed,
                BuildZmqErrorMessage("Failed to send ZeroMQ routing id frame", zmq_errno()),
                error_message);
        }

        const void* payload_data = message.empty() ? static_cast<const void*>("") : static_cast<const void*>(message.data());
        if (zmq_send(socket_, payload_data, message.size(), ZMQ_DONTWAIT) < 0)
        {
            return SetLastError(
                ZmqSocketErrorCode::PayloadSendFailed,
                BuildZmqErrorMessage("Failed to send ZeroMQ payload frame", zmq_errno()),
                error_message);
        }

        metrics_.RecordSentMessage(message.size());
        ClearLastError();
        return ZmqSocketErrorCode::None;
    }

    void SetMessageHandler(ZmqRoutedMessageHandler handler)
    {
        message_handler_ = std::move(handler);
    }

    void SetStateHandler(ZmqListenerStateHandler handler)
    {
        state_handler_ = std::move(handler);
    }

    [[nodiscard]] bool IsRunning() const noexcept
    {
        return running_;
    }

    [[nodiscard]] ZmqListenerState state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] const ZmqPassiveListenerOptions& options() const noexcept
    {
        return options_;
    }

    [[nodiscard]] std::string_view bound_endpoint() const noexcept
    {
        return bound_endpoint_;
    }

    [[nodiscard]] std::string_view last_error_message() const noexcept
    {
        return last_error_message_;
    }

    [[nodiscard]] ZmqListenerMetricsSnapshot metrics() const noexcept
    {
        return metrics_.Snapshot();
    }

  private:
    [[nodiscard]] ZmqSocketErrorCode ValidateOptions(std::string* error_message)
    {
        if (!context_.IsValid() || context_.native_handle() == nullptr)
        {
            return SetLastError(
                ZmqSocketErrorCode::ContextNotInitialized,
                "ZeroMQ context is not initialized.",
                error_message);
        }

        if (options_.local_endpoint.empty())
        {
            return SetLastError(
                ZmqSocketErrorCode::EndpointEmpty,
                "ZeroMQ passive listener local_endpoint must not be empty.",
                error_message);
        }

        if (options_.poll_interval <= std::chrono::milliseconds::zero())
        {
            return SetLastError(
                ZmqSocketErrorCode::PollIntervalMustBePositive,
                "ZeroMQ passive listener poll_interval must be greater than zero.",
                error_message);
        }

        if (options_.send_high_water_mark < 0 || options_.receive_high_water_mark < 0)
        {
            return SetLastError(
                ZmqSocketErrorCode::HighWaterMarkNegative,
                "ZeroMQ passive listener high water marks must not be negative.",
                error_message);
        }

        if (options_.handshake_interval_ms < 0)
        {
            return SetLastError(
                ZmqSocketErrorCode::TimingOptionNegative,
                "ZeroMQ passive listener handshake_interval_ms must not be negative.",
                error_message);
        }

        return ZmqSocketErrorCode::None;
    }

    [[nodiscard]] ZmqSocketErrorCode OpenSockets(std::string* error_message)
    {
        std::string option_error;

        socket_ = zmq_socket(context_.native_handle(), ZMQ_ROUTER);
        if (socket_ == nullptr)
        {
            return SetLastError(
                ZmqSocketErrorCode::SocketCreateFailed,
                BuildZmqErrorMessage("Failed to create ZeroMQ ROUTER socket", zmq_errno()),
                error_message);
        }

        if (!SetIntegerSocketOption(socket_, ZMQ_LINGER, 0, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_ROUTER_MANDATORY, 1, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_SNDHWM, options_.send_high_water_mark, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_RCVHWM, options_.receive_high_water_mark, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_HANDSHAKE_IVL, options_.handshake_interval_ms, &option_error))
        {
            return SetLastError(ZmqSocketErrorCode::SocketOptionFailed, std::move(option_error), error_message);
        }

        monitor_endpoint_ = MakeMonitorEndpoint();
        if (zmq_socket_monitor(socket_, monitor_endpoint_.c_str(), kMonitorEvents) != 0)
        {
            return SetLastError(
                ZmqSocketErrorCode::MonitorEnableFailed,
                BuildZmqErrorMessage("Failed to enable ZeroMQ socket monitor", zmq_errno()),
                error_message);
        }

        monitor_socket_ = zmq_socket(context_.native_handle(), ZMQ_PAIR);
        if (monitor_socket_ == nullptr)
        {
            return SetLastError(
                ZmqSocketErrorCode::MonitorSocketCreateFailed,
                BuildZmqErrorMessage("Failed to create ZeroMQ monitor socket", zmq_errno()),
                error_message);
        }

        if (!SetIntegerSocketOption(monitor_socket_, ZMQ_LINGER, 0, &option_error))
        {
            return SetLastError(ZmqSocketErrorCode::SocketOptionFailed, std::move(option_error), error_message);
        }

        if (zmq_connect(monitor_socket_, monitor_endpoint_.c_str()) != 0)
        {
            return SetLastError(
                ZmqSocketErrorCode::MonitorSocketConnectFailed,
                BuildZmqErrorMessage("Failed to connect ZeroMQ monitor socket", zmq_errno()),
                error_message);
        }

        if (zmq_bind(socket_, options_.local_endpoint.c_str()) != 0)
        {
            return SetLastError(
                ZmqSocketErrorCode::BindFailed,
                BuildZmqErrorMessage("Failed to bind ZeroMQ ROUTER socket", zmq_errno()),
                error_message);
        }

        if (!QueryLastEndpoint(socket_, &bound_endpoint_, &option_error))
        {
            return SetLastError(ZmqSocketErrorCode::LastEndpointQueryFailed, std::move(option_error), error_message);
        }

        return ZmqSocketErrorCode::None;
    }

    void CloseSockets() noexcept
    {
        if (socket_ != nullptr)
        {
            (void)zmq_socket_monitor(socket_, nullptr, 0);
        }

        if (monitor_socket_ != nullptr)
        {
            (void)zmq_close(monitor_socket_);
            monitor_socket_ = nullptr;
        }

        if (socket_ != nullptr)
        {
            (void)zmq_close(socket_);
            socket_ = nullptr;
        }

        monitor_endpoint_.clear();
        bound_endpoint_.clear();
    }

    void SchedulePump()
    {
        if (!running_)
        {
            return;
        }

        pump_timer_.expires_after(options_.poll_interval);
        std::weak_ptr<Impl> weak_self = shared_from_this();
        pump_timer_.async_wait([weak_self](const asio::error_code& error_code) {
            if (const auto self = weak_self.lock())
            {
                self->HandlePump(error_code);
            }
        });
    }

    void HandlePump(const asio::error_code& error_code)
    {
        if (error_code == asio::error::operation_aborted || !running_)
        {
            return;
        }

        if (error_code)
        {
            (void)SetLastError(
                ZmqSocketErrorCode::TimerPumpFailed,
                "Asio timer error while driving ZeroMQ passive listener: " + error_code.message(),
                nullptr);
            return;
        }

        PollMonitor();
        if (!running_)
        {
            return;
        }

        PollIncomingMessages();
        if (!running_)
        {
            return;
        }

        SchedulePump();
    }

    void PollMonitor()
    {
        while (running_ && monitor_socket_ != nullptr)
        {
            std::uint16_t event = 0;
            std::uint32_t event_value = 0;
            if (!ReceiveMonitorEvent(&event, &event_value))
            {
                return;
            }

            switch (event)
            {
            case ZMQ_EVENT_ACCEPTED:
                HandleAcceptedConnection(event_value);
                break;
            case ZMQ_EVENT_DISCONNECTED:
                HandleDisconnectedConnection(event_value);
                break;
            case ZMQ_EVENT_MONITOR_STOPPED:
            default:
                break;
            }
        }
    }

    void PollIncomingMessages()
    {
        while (running_ && socket_ != nullptr)
        {
            std::vector<std::byte> routing_id;
            std::vector<std::byte> message;
            if (!ReceiveMessage(&routing_id, &message))
            {
                return;
            }

            if (message.empty())
            {
                continue;
            }

            metrics_.RecordReceivedMessage(message.size());
            if (message_handler_)
            {
                message_handler_(std::move(routing_id), std::move(message));
                if (!running_)
                {
                    return;
                }
            }
        }
    }

    void HandleAcceptedConnection(std::uint32_t socket_id)
    {
        if (active_connection_ids_.insert(socket_id).second)
        {
            metrics_.SetActiveConnectionCount(active_connection_ids_.size());
        }
    }

    void HandleDisconnectedConnection(std::uint32_t socket_id)
    {
        if (active_connection_ids_.erase(socket_id) > 0u)
        {
            metrics_.SetActiveConnectionCount(active_connection_ids_.size());
        }
    }

    [[nodiscard]] bool ReceiveMonitorEvent(std::uint16_t* event, std::uint32_t* value)
    {
        if (event == nullptr || value == nullptr || monitor_socket_ == nullptr)
        {
            return false;
        }

        zmq_msg_t frame;
        zmq_msg_init(&frame);
        const int receive_result = zmq_msg_recv(&frame, monitor_socket_, ZMQ_DONTWAIT);
        if (receive_result < 0)
        {
            const int error_code = zmq_errno();
            zmq_msg_close(&frame);
            if (error_code == EAGAIN)
            {
                return false;
            }

            (void)SetLastError(
                ZmqSocketErrorCode::MonitorReceiveFailed,
                BuildZmqErrorMessage("Failed to receive ZeroMQ monitor event", error_code),
                nullptr);
            return false;
        }

        const bool has_more = zmq_msg_more(&frame) != 0;
        const std::size_t frame_size = zmq_msg_size(&frame);
        if (!has_more || frame_size < kMonitorEventFrameSize)
        {
            zmq_msg_close(&frame);
            DrainMultipartFrames(monitor_socket_);
            (void)SetLastError(
                ZmqSocketErrorCode::MonitorReceiveFailed,
                "ZeroMQ monitor emitted an unexpected event frame.",
                nullptr);
            return false;
        }

        const auto* data = static_cast<const std::uint8_t*>(zmq_msg_data(&frame));
        std::memcpy(event, data, sizeof(*event));
        std::memcpy(value, data + sizeof(*event), sizeof(*value));
        zmq_msg_close(&frame);

        zmq_msg_t address_frame;
        zmq_msg_init(&address_frame);
        const int address_result = zmq_msg_recv(&address_frame, monitor_socket_, ZMQ_DONTWAIT);
        if (address_result < 0)
        {
            const int error_code = zmq_errno();
            zmq_msg_close(&address_frame);
            (void)SetLastError(
                ZmqSocketErrorCode::MonitorReceiveFailed,
                BuildZmqErrorMessage("Failed to receive ZeroMQ monitor address frame", error_code),
                nullptr);
            return false;
        }

        if (zmq_msg_more(&address_frame) != 0)
        {
            zmq_msg_close(&address_frame);
            DrainMultipartFrames(monitor_socket_);
            (void)SetLastError(
                ZmqSocketErrorCode::MonitorReceiveFailed,
                "ZeroMQ monitor emitted an unexpected multipart address frame.",
                nullptr);
            return false;
        }

        zmq_msg_close(&address_frame);
        return true;
    }

    [[nodiscard]] bool ReceiveMessage(std::vector<std::byte>* routing_id, std::vector<std::byte>* message)
    {
        if (routing_id == nullptr || message == nullptr || socket_ == nullptr)
        {
            return false;
        }

        zmq_msg_t routing_frame;
        zmq_msg_init(&routing_frame);
        const int routing_result = zmq_msg_recv(&routing_frame, socket_, ZMQ_DONTWAIT);
        if (routing_result < 0)
        {
            const int error_code = zmq_errno();
            zmq_msg_close(&routing_frame);
            if (error_code == EAGAIN)
            {
                return false;
            }

            (void)SetLastError(
                ZmqSocketErrorCode::ReceiveFailed,
                BuildZmqErrorMessage("Failed to receive ZeroMQ routing id frame", error_code),
                nullptr);
            return false;
        }

        if (zmq_msg_more(&routing_frame) == 0)
        {
            zmq_msg_close(&routing_frame);
            (void)SetLastError(
                ZmqSocketErrorCode::PayloadMissing,
                "ZeroMQ passive listener expected a payload frame after routing id.",
                nullptr);
            return false;
        }

        const auto* routing_data = static_cast<const std::byte*>(zmq_msg_data(&routing_frame));
        const std::size_t routing_size = zmq_msg_size(&routing_frame);
        routing_id->assign(routing_data, routing_data + routing_size);
        zmq_msg_close(&routing_frame);

        zmq_msg_t payload_frame;
        zmq_msg_init(&payload_frame);
        const int payload_result = zmq_msg_recv(&payload_frame, socket_, ZMQ_DONTWAIT);
        if (payload_result < 0)
        {
            const int error_code = zmq_errno();
            zmq_msg_close(&payload_frame);
            (void)SetLastError(
                ZmqSocketErrorCode::ReceiveFailed,
                BuildZmqErrorMessage("Failed to receive ZeroMQ payload frame", error_code),
                nullptr);
            return false;
        }

        if (zmq_msg_more(&payload_frame) != 0)
        {
            zmq_msg_close(&payload_frame);
            DrainMultipartFrames(socket_);
            (void)SetLastError(
                ZmqSocketErrorCode::MultipartNotSupported,
                "ZeroMQ passive listener only supports routing id plus single-frame payload.",
                nullptr);
            return false;
        }

        const auto* payload_data = static_cast<const std::byte*>(zmq_msg_data(&payload_frame));
        const std::size_t payload_size = zmq_msg_size(&payload_frame);
        message->assign(payload_data, payload_data + payload_size);
        zmq_msg_close(&payload_frame);
        ClearLastError();
        return true;
    }

    void DrainMultipartFrames(void* socket) noexcept
    {
        if (socket == nullptr)
        {
            return;
        }

        while (true)
        {
            zmq_msg_t frame;
            zmq_msg_init(&frame);
            const int receive_result = zmq_msg_recv(&frame, socket, ZMQ_DONTWAIT);
            if (receive_result < 0)
            {
                zmq_msg_close(&frame);
                return;
            }

            const bool has_more = zmq_msg_more(&frame) != 0;
            zmq_msg_close(&frame);
            if (!has_more)
            {
                return;
            }
        }
    }

    void UpdateState(ZmqListenerState new_state)
    {
        if (state_ == new_state)
        {
            return;
        }

        state_ = new_state;
        if (state_handler_)
        {
            state_handler_(state_);
        }
    }

    [[nodiscard]] ZmqSocketErrorCode SetLastError(
        ZmqSocketErrorCode code,
        std::string message,
        std::string* error_message)
    {
        last_error_message_ = std::move(message);
        if (error_message != nullptr)
        {
            *error_message = last_error_message_;
        }
        return code;
    }

    void ClearLastError() noexcept
    {
        last_error_message_.clear();
    }

    ZmqContext& context_;
    ZmqPassiveListenerOptions options_{};
    asio::steady_timer pump_timer_;
    void* socket_{nullptr};
    void* monitor_socket_{nullptr};
    std::string monitor_endpoint_{};
    std::string bound_endpoint_{};
    ZmqRoutedMessageHandler message_handler_{};
    ZmqListenerStateHandler state_handler_{};
    std::unordered_set<std::uint32_t> active_connection_ids_{};
    ZmqListenerMetrics metrics_{};
    bool running_{false};
    ZmqListenerState state_{ZmqListenerState::Stopped};
    std::string last_error_message_{};
};

ZmqPassiveListener::ZmqPassiveListener(
    asio::io_context& io_context,
    ZmqContext& context,
    ZmqPassiveListenerOptions options)
    : impl_(std::make_shared<Impl>(io_context, context, std::move(options)))
{
}

ZmqPassiveListener::~ZmqPassiveListener()
{
    if (impl_ != nullptr)
    {
        impl_->Stop();
    }
}

ZmqSocketErrorCode ZmqPassiveListener::Start(std::string* error_message)
{
    if (impl_ == nullptr)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string(ZmqSocketErrorMessage(ZmqSocketErrorCode::InvalidState));
        }
        return ZmqSocketErrorCode::InvalidState;
    }

    return impl_->Start(error_message);
}

void ZmqPassiveListener::Stop() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->Stop();
    }
}

ZmqSocketErrorCode ZmqPassiveListener::Send(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> message,
    std::string* error_message)
{
    if (impl_ == nullptr)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string(ZmqSocketErrorMessage(ZmqSocketErrorCode::InvalidState));
        }
        return ZmqSocketErrorCode::InvalidState;
    }

    return impl_->Send(routing_id, message, error_message);
}

void ZmqPassiveListener::SetMessageHandler(ZmqRoutedMessageHandler handler)
{
    if (impl_ != nullptr)
    {
        impl_->SetMessageHandler(std::move(handler));
    }
}

void ZmqPassiveListener::SetStateHandler(ZmqListenerStateHandler handler)
{
    if (impl_ != nullptr)
    {
        impl_->SetStateHandler(std::move(handler));
    }
}

bool ZmqPassiveListener::IsRunning() const noexcept
{
    return impl_ != nullptr && impl_->IsRunning();
}

ZmqListenerState ZmqPassiveListener::state() const noexcept
{
    return impl_ != nullptr ? impl_->state() : ZmqListenerState::Stopped;
}

const ZmqPassiveListenerOptions& ZmqPassiveListener::options() const noexcept
{
    return impl_->options();
}

std::string_view ZmqPassiveListener::bound_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->bound_endpoint() : std::string_view{};
}

std::string_view ZmqPassiveListener::last_error_message() const noexcept
{
    return impl_ != nullptr ? impl_->last_error_message() : std::string_view{};
}

ZmqListenerMetricsSnapshot ZmqPassiveListener::metrics() const noexcept
{
    return impl_ != nullptr ? impl_->metrics() : ZmqListenerMetricsSnapshot{};
}

} // namespace xs::ipc

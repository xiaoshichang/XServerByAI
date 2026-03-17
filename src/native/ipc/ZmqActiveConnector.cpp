#include "ZmqActiveConnector.h"

#include <asio/error.hpp>
#include <asio/steady_timer.hpp>

#include <zmq.h>

#include <atomic>
#include <cstring>
#include <utility>

namespace xs::ipc
{
namespace
{

constexpr std::size_t kMonitorEventFrameSize = sizeof(std::uint16_t) + sizeof(std::uint32_t);
constexpr int kMonitorEvents =
    ZMQ_EVENT_CONNECTED |
    ZMQ_EVENT_CONNECT_DELAYED |
    ZMQ_EVENT_CONNECT_RETRIED |
    ZMQ_EVENT_CLOSED |
    ZMQ_EVENT_CLOSE_FAILED |
    ZMQ_EVENT_DISCONNECTED |
    ZMQ_EVENT_HANDSHAKE_SUCCEEDED |
    ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL |
    ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL |
    ZMQ_EVENT_HANDSHAKE_FAILED_AUTH |
    ZMQ_EVENT_MONITOR_STOPPED;

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
    return "inproc://xs-ipc-active-monitor-" + std::to_string(sequence);
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

bool SetBinarySocketOption(void* socket,
                           int option,
                           const void* value,
                           std::size_t value_size,
                           std::string* error_message)
{
    if (zmq_setsockopt(socket, option, value, value_size) == 0)
    {
        return true;
    }

    if (error_message != nullptr)
    {
        *error_message = BuildZmqErrorMessage("Failed to configure ZeroMQ socket option", zmq_errno());
    }
    return false;
}

} // namespace

std::string_view ZmqConnectionStateName(ZmqConnectionState state) noexcept
{
    switch (state)
    {
    case ZmqConnectionState::Stopped:
        return "Stopped";
    case ZmqConnectionState::Connecting:
        return "Connecting";
    case ZmqConnectionState::Connected:
        return "Connected";
    case ZmqConnectionState::Disconnected:
        return "Disconnected";
    }

    return "Unknown";
}

class ZmqActiveConnector::Impl final : public std::enable_shared_from_this<Impl>
{
  public:
    Impl(asio::io_context& io_context, ZmqContext& context, ZmqActiveConnectorOptions options)
        : context_(context), options_(std::move(options)), pump_timer_(io_context)
    {
    }

    [[nodiscard]] bool Start(std::string* error_message)
    {
        if (running_)
        {
            return SetLastError("ZeroMQ active connector is already running.", error_message);
        }

        if (!ValidateOptions(error_message))
        {
            return false;
        }

        if (!OpenSockets(error_message))
        {
            CloseSockets();
            return false;
        }

        running_ = true;
        ClearLastError();
        UpdateState(ZmqConnectionState::Connecting);
        SchedulePump();
        return true;
    }

    void Stop() noexcept
    {
        if (running_)
        {
            running_ = false;
            (void)pump_timer_.cancel();
        }

        CloseSockets();
        ClearLastError();
        UpdateState(ZmqConnectionState::Stopped);
    }

    [[nodiscard]] bool Send(std::span<const std::byte> message, std::string* error_message)
    {
        if (!running_ || socket_ == nullptr)
        {
            return SetLastError("ZeroMQ active connector must be started before Send().", error_message);
        }

        const void* data = message.empty() ? static_cast<const void*>("") : static_cast<const void*>(message.data());
        const int send_result = zmq_send(socket_, data, message.size(), ZMQ_DONTWAIT);
        if (send_result < 0)
        {
            return SetLastError(BuildZmqErrorMessage("Failed to send ZeroMQ message", zmq_errno()), error_message);
        }

        ClearLastError();
        return true;
    }

    void SetMessageHandler(ZmqMessageHandler handler)
    {
        message_handler_ = std::move(handler);
    }

    void SetStateHandler(ZmqStateHandler handler)
    {
        state_handler_ = std::move(handler);
    }

    [[nodiscard]] bool IsRunning() const noexcept
    {
        return running_;
    }

    [[nodiscard]] ZmqConnectionState state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] const ZmqActiveConnectorOptions& options() const noexcept
    {
        return options_;
    }

    [[nodiscard]] std::string_view last_error_message() const noexcept
    {
        return last_error_message_;
    }

  private:
    [[nodiscard]] bool ValidateOptions(std::string* error_message)
    {
        if (!context_.IsValid() || context_.native_handle() == nullptr)
        {
            return SetLastError("ZeroMQ context is not initialized.", error_message);
        }

        if (options_.remote_endpoint.empty())
        {
            return SetLastError("ZeroMQ active connector remote_endpoint must not be empty.", error_message);
        }

        if (options_.poll_interval <= std::chrono::milliseconds::zero())
        {
            return SetLastError("ZeroMQ active connector poll_interval must be greater than zero.", error_message);
        }

        if (options_.send_high_water_mark < 0 || options_.receive_high_water_mark < 0)
        {
            return SetLastError("ZeroMQ active connector high water marks must not be negative.", error_message);
        }

        if (options_.reconnect_interval_ms < 0 || options_.reconnect_interval_max_ms < 0 ||
            options_.handshake_interval_ms < 0)
        {
            return SetLastError("ZeroMQ active connector timing options must not be negative.", error_message);
        }

        return true;
    }

    [[nodiscard]] bool OpenSockets(std::string* error_message)
    {
        std::string option_error;

        socket_ = zmq_socket(context_.native_handle(), ZMQ_DEALER);
        if (socket_ == nullptr)
        {
            return SetLastError(BuildZmqErrorMessage("Failed to create ZeroMQ DEALER socket", zmq_errno()), error_message);
        }

        if (!SetIntegerSocketOption(socket_, ZMQ_LINGER, 0, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_IMMEDIATE, 1, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_SNDHWM, options_.send_high_water_mark, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_RCVHWM, options_.receive_high_water_mark, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_RECONNECT_IVL, options_.reconnect_interval_ms, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_RECONNECT_IVL_MAX, options_.reconnect_interval_max_ms, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_HANDSHAKE_IVL, options_.handshake_interval_ms, &option_error))
        {
            return SetLastError(std::move(option_error), error_message);
        }

        if (!options_.routing_id.empty() &&
            !SetBinarySocketOption(
                socket_, ZMQ_ROUTING_ID, options_.routing_id.data(), options_.routing_id.size(), &option_error))
        {
            return SetLastError(std::move(option_error), error_message);
        }

        monitor_endpoint_ = MakeMonitorEndpoint();
        if (zmq_socket_monitor(socket_, monitor_endpoint_.c_str(), kMonitorEvents) != 0)
        {
            return SetLastError(
                BuildZmqErrorMessage("Failed to enable ZeroMQ socket monitor", zmq_errno()), error_message);
        }

        monitor_socket_ = zmq_socket(context_.native_handle(), ZMQ_PAIR);
        if (monitor_socket_ == nullptr)
        {
            return SetLastError(
                BuildZmqErrorMessage("Failed to create ZeroMQ monitor socket", zmq_errno()), error_message);
        }

        if (!SetIntegerSocketOption(monitor_socket_, ZMQ_LINGER, 0, &option_error))
        {
            return SetLastError(std::move(option_error), error_message);
        }

        if (zmq_connect(monitor_socket_, monitor_endpoint_.c_str()) != 0)
        {
            return SetLastError(
                BuildZmqErrorMessage("Failed to connect ZeroMQ monitor socket", zmq_errno()), error_message);
        }

        if (zmq_connect(socket_, options_.remote_endpoint.c_str()) != 0)
        {
            return SetLastError(BuildZmqErrorMessage("Failed to connect ZeroMQ DEALER socket", zmq_errno()), error_message);
        }

        return true;
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
            (void)SetLastError("Asio timer error while driving ZeroMQ active connector: " + error_code.message(), nullptr);
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
            case ZMQ_EVENT_CONNECTED:
            case ZMQ_EVENT_HANDSHAKE_SUCCEEDED:
                UpdateState(ZmqConnectionState::Connected);
                break;
            case ZMQ_EVENT_CONNECT_DELAYED:
            case ZMQ_EVENT_CONNECT_RETRIED:
                if (state_ != ZmqConnectionState::Connected)
                {
                    UpdateState(ZmqConnectionState::Connecting);
                }
                break;
            case ZMQ_EVENT_DISCONNECTED:
            case ZMQ_EVENT_CLOSED:
            case ZMQ_EVENT_CLOSE_FAILED:
            case ZMQ_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
            case ZMQ_EVENT_HANDSHAKE_FAILED_PROTOCOL:
            case ZMQ_EVENT_HANDSHAKE_FAILED_AUTH:
                UpdateState(ZmqConnectionState::Disconnected);
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
            std::vector<std::byte> message;
            if (!ReceiveMessage(&message))
            {
                return;
            }

            if (message_handler_)
            {
                message_handler_(std::move(message));
                if (!running_)
                {
                    return;
                }
            }
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

            (void)SetLastError(BuildZmqErrorMessage("Failed to receive ZeroMQ monitor event", error_code), nullptr);
            return false;
        }

        const bool has_more = zmq_msg_more(&frame) != 0;
        const std::size_t frame_size = zmq_msg_size(&frame);
        if (!has_more || frame_size < kMonitorEventFrameSize)
        {
            zmq_msg_close(&frame);
            DrainMultipartFrames(monitor_socket_);
            (void)SetLastError("ZeroMQ monitor emitted an unexpected event frame.", nullptr);
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
                BuildZmqErrorMessage("Failed to receive ZeroMQ monitor address frame", error_code), nullptr);
            return false;
        }

        if (zmq_msg_more(&address_frame) != 0)
        {
            zmq_msg_close(&address_frame);
            DrainMultipartFrames(monitor_socket_);
            (void)SetLastError("ZeroMQ monitor emitted an unexpected multipart address frame.", nullptr);
            return false;
        }

        zmq_msg_close(&address_frame);
        return true;
    }

    [[nodiscard]] bool ReceiveMessage(std::vector<std::byte>* message)
    {
        if (message == nullptr || socket_ == nullptr)
        {
            return false;
        }

        zmq_msg_t frame;
        zmq_msg_init(&frame);
        const int receive_result = zmq_msg_recv(&frame, socket_, ZMQ_DONTWAIT);
        if (receive_result < 0)
        {
            const int error_code = zmq_errno();
            zmq_msg_close(&frame);
            if (error_code == EAGAIN)
            {
                return false;
            }

            (void)SetLastError(BuildZmqErrorMessage("Failed to receive ZeroMQ message", error_code), nullptr);
            return false;
        }

        if (zmq_msg_more(&frame) != 0)
        {
            zmq_msg_close(&frame);
            DrainMultipartFrames(socket_);
            (void)SetLastError("ZeroMQ active connector only supports single-frame messages.", nullptr);
            return false;
        }

        const auto* data = static_cast<const std::byte*>(zmq_msg_data(&frame));
        const std::size_t size = zmq_msg_size(&frame);
        message->assign(data, data + size);
        zmq_msg_close(&frame);
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

    void UpdateState(ZmqConnectionState new_state)
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

    [[nodiscard]] bool SetLastError(std::string message, std::string* error_message)
    {
        last_error_message_ = std::move(message);
        if (error_message != nullptr)
        {
            *error_message = last_error_message_;
        }
        return false;
    }

    void ClearLastError() noexcept
    {
        last_error_message_.clear();
    }

    ZmqContext& context_;
    ZmqActiveConnectorOptions options_{};
    asio::steady_timer pump_timer_;
    void* socket_{nullptr};
    void* monitor_socket_{nullptr};
    std::string monitor_endpoint_{};
    ZmqMessageHandler message_handler_{};
    ZmqStateHandler state_handler_{};
    bool running_{false};
    ZmqConnectionState state_{ZmqConnectionState::Stopped};
    std::string last_error_message_{};
};

ZmqActiveConnector::ZmqActiveConnector(asio::io_context& io_context, ZmqContext& context, ZmqActiveConnectorOptions options)
    : impl_(std::make_shared<Impl>(io_context, context, std::move(options)))
{
}

ZmqActiveConnector::~ZmqActiveConnector()
{
    if (impl_ != nullptr)
    {
        impl_->Stop();
    }
}

bool ZmqActiveConnector::Start(std::string* error_message)
{
    return impl_ != nullptr && impl_->Start(error_message);
}

void ZmqActiveConnector::Stop() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->Stop();
    }
}

bool ZmqActiveConnector::Send(std::span<const std::byte> message, std::string* error_message)
{
    return impl_ != nullptr && impl_->Send(message, error_message);
}

void ZmqActiveConnector::SetMessageHandler(ZmqMessageHandler handler)
{
    if (impl_ != nullptr)
    {
        impl_->SetMessageHandler(std::move(handler));
    }
}

void ZmqActiveConnector::SetStateHandler(ZmqStateHandler handler)
{
    if (impl_ != nullptr)
    {
        impl_->SetStateHandler(std::move(handler));
    }
}

bool ZmqActiveConnector::IsRunning() const noexcept
{
    return impl_ != nullptr && impl_->IsRunning();
}

ZmqConnectionState ZmqActiveConnector::state() const noexcept
{
    return impl_ != nullptr ? impl_->state() : ZmqConnectionState::Stopped;
}

const ZmqActiveConnectorOptions& ZmqActiveConnector::options() const noexcept
{
    return impl_->options();
}

std::string_view ZmqActiveConnector::last_error_message() const noexcept
{
    return impl_ != nullptr ? impl_->last_error_message() : std::string_view{};
}

} // namespace xs::ipc

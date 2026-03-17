#include "ZmqPassiveListener.h"

#include <asio/error.hpp>
#include <asio/steady_timer.hpp>

#include <zmq.h>

#include <utility>

namespace xs::ipc
{
namespace
{

[[nodiscard]] std::string BuildZmqErrorMessage(std::string_view prefix, int error_code)
{
    std::string message(prefix);
    message += ": ";
    message += zmq_strerror(error_code);
    return message;
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

    [[nodiscard]] bool Start(std::string* error_message)
    {
        if (running_)
        {
            return SetLastError("ZeroMQ passive listener is already running.", error_message);
        }

        if (!ValidateOptions(error_message))
        {
            return false;
        }

        if (!OpenSocket(error_message))
        {
            CloseSocket();
            return false;
        }

        running_ = true;
        ClearLastError();
        UpdateState(ZmqListenerState::Listening);
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

        CloseSocket();
        ClearLastError();
        UpdateState(ZmqListenerState::Stopped);
    }

    [[nodiscard]] bool Send(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> message,
        std::string* error_message)
    {
        if (!running_ || socket_ == nullptr)
        {
            return SetLastError("ZeroMQ passive listener must be started before Send().", error_message);
        }

        if (routing_id.empty())
        {
            return SetLastError("ZeroMQ passive listener routing_id must not be empty.", error_message);
        }

        const void* routing_data = static_cast<const void*>(routing_id.data());
        if (zmq_send(socket_, routing_data, routing_id.size(), ZMQ_DONTWAIT | ZMQ_SNDMORE) < 0)
        {
            return SetLastError(BuildZmqErrorMessage("Failed to send ZeroMQ routing id frame", zmq_errno()), error_message);
        }

        const void* payload_data = message.empty() ? static_cast<const void*>("") : static_cast<const void*>(message.data());
        if (zmq_send(socket_, payload_data, message.size(), ZMQ_DONTWAIT) < 0)
        {
            return SetLastError(BuildZmqErrorMessage("Failed to send ZeroMQ payload frame", zmq_errno()), error_message);
        }

        ClearLastError();
        return true;
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

  private:
    [[nodiscard]] bool ValidateOptions(std::string* error_message)
    {
        if (!context_.IsValid() || context_.native_handle() == nullptr)
        {
            return SetLastError("ZeroMQ context is not initialized.", error_message);
        }

        if (options_.local_endpoint.empty())
        {
            return SetLastError("ZeroMQ passive listener local_endpoint must not be empty.", error_message);
        }

        if (options_.poll_interval <= std::chrono::milliseconds::zero())
        {
            return SetLastError("ZeroMQ passive listener poll_interval must be greater than zero.", error_message);
        }

        if (options_.send_high_water_mark < 0 || options_.receive_high_water_mark < 0)
        {
            return SetLastError("ZeroMQ passive listener high water marks must not be negative.", error_message);
        }

        if (options_.handshake_interval_ms < 0)
        {
            return SetLastError("ZeroMQ passive listener handshake_interval_ms must not be negative.", error_message);
        }

        return true;
    }

    [[nodiscard]] bool OpenSocket(std::string* error_message)
    {
        std::string option_error;

        socket_ = zmq_socket(context_.native_handle(), ZMQ_ROUTER);
        if (socket_ == nullptr)
        {
            return SetLastError(BuildZmqErrorMessage("Failed to create ZeroMQ ROUTER socket", zmq_errno()), error_message);
        }

        if (!SetIntegerSocketOption(socket_, ZMQ_LINGER, 0, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_ROUTER_MANDATORY, 1, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_SNDHWM, options_.send_high_water_mark, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_RCVHWM, options_.receive_high_water_mark, &option_error) ||
            !SetIntegerSocketOption(socket_, ZMQ_HANDSHAKE_IVL, options_.handshake_interval_ms, &option_error))
        {
            return SetLastError(std::move(option_error), error_message);
        }

        if (zmq_bind(socket_, options_.local_endpoint.c_str()) != 0)
        {
            return SetLastError(BuildZmqErrorMessage("Failed to bind ZeroMQ ROUTER socket", zmq_errno()), error_message);
        }

        if (!QueryLastEndpoint(socket_, &bound_endpoint_, &option_error))
        {
            return SetLastError(std::move(option_error), error_message);
        }

        return true;
    }

    void CloseSocket() noexcept
    {
        if (socket_ != nullptr)
        {
            (void)zmq_close(socket_);
            socket_ = nullptr;
        }

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
            (void)SetLastError("Asio timer error while driving ZeroMQ passive listener: " + error_code.message(), nullptr);
            return;
        }

        PollIncomingMessages();
        if (!running_)
        {
            return;
        }

        SchedulePump();
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

            (void)SetLastError(BuildZmqErrorMessage("Failed to receive ZeroMQ routing id frame", error_code), nullptr);
            return false;
        }

        if (zmq_msg_more(&routing_frame) == 0)
        {
            zmq_msg_close(&routing_frame);
            (void)SetLastError("ZeroMQ passive listener expected a payload frame after routing id.", nullptr);
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
            (void)SetLastError(BuildZmqErrorMessage("Failed to receive ZeroMQ payload frame", error_code), nullptr);
            return false;
        }

        if (zmq_msg_more(&payload_frame) != 0)
        {
            zmq_msg_close(&payload_frame);
            DrainMultipartFrames(socket_);
            (void)SetLastError("ZeroMQ passive listener only supports routing id plus single-frame payload.", nullptr);
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
    ZmqPassiveListenerOptions options_{};
    asio::steady_timer pump_timer_;
    void* socket_{nullptr};
    std::string bound_endpoint_{};
    ZmqRoutedMessageHandler message_handler_{};
    ZmqListenerStateHandler state_handler_{};
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

bool ZmqPassiveListener::Start(std::string* error_message)
{
    return impl_ != nullptr && impl_->Start(error_message);
}

void ZmqPassiveListener::Stop() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->Stop();
    }
}

bool ZmqPassiveListener::Send(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> message,
    std::string* error_message)
{
    return impl_ != nullptr && impl_->Send(routing_id, message, error_message);
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

} // namespace xs::ipc
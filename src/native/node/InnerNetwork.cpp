#include "InnerNetwork.h"

#include "ZmqContext.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

void ClearError(std::string& error_message) noexcept
{
    error_message.clear();
}

NodeErrorCode SetError(
    std::string& error_message,
    NodeErrorCode code,
    std::string message,
    std::string_view fallback_message = {})
{
    if (message.empty())
    {
        if (!fallback_message.empty())
        {
            error_message = std::string(fallback_message);
        }
        else
        {
            error_message = std::string(NodeErrorMessage(code));
        }
    }
    else
    {
        error_message = std::move(message);
    }

    return code;
}

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

NodeErrorCode MapSocketErrorToNodeError(ipc::ZmqSocketErrorCode code) noexcept
{
    switch (code)
    {
    case ipc::ZmqSocketErrorCode::None:
        return NodeErrorCode::None;
    case ipc::ZmqSocketErrorCode::InvalidState:
    case ipc::ZmqSocketErrorCode::AlreadyRunning:
    case ipc::ZmqSocketErrorCode::ContextNotInitialized:
    case ipc::ZmqSocketErrorCode::EndpointEmpty:
    case ipc::ZmqSocketErrorCode::PollIntervalMustBePositive:
    case ipc::ZmqSocketErrorCode::HighWaterMarkNegative:
    case ipc::ZmqSocketErrorCode::TimingOptionNegative:
    case ipc::ZmqSocketErrorCode::NotStarted:
    case ipc::ZmqSocketErrorCode::EmptyRoutingId:
        return NodeErrorCode::InvalidArgument;
    case ipc::ZmqSocketErrorCode::SocketCreateFailed:
    case ipc::ZmqSocketErrorCode::SocketOptionFailed:
    case ipc::ZmqSocketErrorCode::MonitorEnableFailed:
    case ipc::ZmqSocketErrorCode::MonitorSocketCreateFailed:
    case ipc::ZmqSocketErrorCode::MonitorSocketConnectFailed:
    case ipc::ZmqSocketErrorCode::ConnectFailed:
    case ipc::ZmqSocketErrorCode::BindFailed:
    case ipc::ZmqSocketErrorCode::LastEndpointQueryFailed:
    case ipc::ZmqSocketErrorCode::RoutingIdSendFailed:
    case ipc::ZmqSocketErrorCode::PayloadSendFailed:
    case ipc::ZmqSocketErrorCode::MessageSendFailed:
    case ipc::ZmqSocketErrorCode::ReceiveFailed:
    case ipc::ZmqSocketErrorCode::MonitorReceiveFailed:
    case ipc::ZmqSocketErrorCode::MultipartNotSupported:
    case ipc::ZmqSocketErrorCode::PayloadMissing:
    case ipc::ZmqSocketErrorCode::TimerPumpFailed:
        return NodeErrorCode::NodeRunFailed;
    }

    return NodeErrorCode::NodeRunFailed;
}

} // namespace

class InnerNetwork::Impl final
{
  public:
    Impl(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger, InnerNetworkOptions options)
        : event_loop_(event_loop), logger_(logger), options_(std::move(options))
    {
    }

    [[nodiscard]] NodeErrorCode Init()
    {
        if (initialized_)
        {
            return SetError(last_error_message_, NodeErrorCode::InvalidArgument, "InnerNetwork is already initialized.");
        }

        if (options_.mode == InnerNetworkMode::Disabled)
        {
            initialized_ = true;
            ClearError(last_error_message_);
            return NodeErrorCode::None;
        }

        if (options_.local_endpoint.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork local_endpoint must not be empty.");
        }

        context_ = std::make_unique<ipc::ZmqContext>();
        if (!context_->IsValid())
        {
            const std::string initialization_error = std::string(context_->initialization_error());
            context_.reset();
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeInitFailed,
                "Failed to initialize inner network ZeroMQ context: " + initialization_error);
        }

        ipc::ZmqPassiveListenerOptions listener_options;
        listener_options.local_endpoint = options_.local_endpoint;

        listener_ = std::make_unique<ipc::ZmqPassiveListener>(event_loop_.context(), *context_, std::move(listener_options));
        listener_->SetStateHandler([this](ipc::ZmqListenerState state) {
            try
            {
                HandleStateChange(state);
            }
            catch (...)
            {
            }
        });
        listener_->SetMessageHandler([this](std::vector<std::byte> routing_id, std::vector<std::byte> payload) {
            try
            {
                HandleMessage(std::move(routing_id), std::move(payload));
            }
            catch (...)
            {
            }
        });

        initialized_ = true;
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Run()
    {
        if (!initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork must be initialized before Run().");
        }

        if (options_.mode == InnerNetworkMode::Disabled)
        {
            ClearError(last_error_message_);
            return NodeErrorCode::None;
        }

        if (listener_ == nullptr)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork listener is not configured.");
        }

        if (listener_->IsRunning())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork listener is already running.");
        }

        std::string listener_error;
        const ipc::ZmqSocketErrorCode start_result = listener_->Start(&listener_error);
        if (start_result != ipc::ZmqSocketErrorCode::None)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                "Failed to start inner network listener: " + listener_error);
        }

        bound_endpoint_ = std::string(listener_->bound_endpoint());
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"configuredEndpoint", options_.local_endpoint},
            xs::core::LogContextField{"boundEndpoint", bound_endpoint_},
        };
        LogInfo("Inner network listener started.", context);

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Uninit() noexcept
    {
        if (!initialized_)
        {
            context_.reset();
            listener_.reset();
            bound_endpoint_.clear();
            message_handler_ = {};
            ClearError(last_error_message_);
            return NodeErrorCode::None;
        }

        if (listener_ != nullptr && listener_->IsRunning())
        {
            const ipc::ZmqListenerMetricsSnapshot snapshot = listener_->metrics();
            const std::string configured_endpoint = options_.local_endpoint;
            const std::string bound_endpoint = std::string(listener_->bound_endpoint());

            try
            {
                const std::array<xs::core::LogContextField, 4> context{
                    xs::core::LogContextField{"configuredEndpoint", configured_endpoint},
                    xs::core::LogContextField{"boundEndpoint", bound_endpoint},
                    xs::core::LogContextField{"receivedMessages", ToString(snapshot.received_message_count)},
                    xs::core::LogContextField{"activeConnections", ToString(snapshot.active_connection_count)},
                };
                LogInfo("Inner network listener stopping.", context);
            }
            catch (...)
            {
            }

            listener_->Stop();

            try
            {
                const std::array<xs::core::LogContextField, 2> context{
                    xs::core::LogContextField{"configuredEndpoint", configured_endpoint},
                    xs::core::LogContextField{"boundEndpoint", bound_endpoint},
                };
                LogInfo("Inner network listener stopped.", context);
            }
            catch (...)
            {
            }
        }

        listener_.reset();
        context_.reset();
        bound_endpoint_.clear();
        message_handler_ = {};
        initialized_ = false;
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Send(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload)
    {
        if (!initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork must be initialized before Send().");
        }

        if (options_.mode != InnerNetworkMode::PassiveListener || listener_ == nullptr)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork Send() requires passive-listener mode.");
        }

        std::string listener_error;
        const ipc::ZmqSocketErrorCode send_result = listener_->Send(routing_id, payload, &listener_error);
        if (send_result != ipc::ZmqSocketErrorCode::None)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                "Failed to send inner network payload: " + listener_error);
        }

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    void SetMessageHandler(InnerNetworkMessageHandler handler)
    {
        message_handler_ = std::move(handler);
    }

    [[nodiscard]] bool IsRunning() const noexcept
    {
        return listener_ != nullptr && listener_->IsRunning();
    }

    [[nodiscard]] InnerNetworkMode mode() const noexcept
    {
        return options_.mode;
    }

    [[nodiscard]] ipc::ZmqListenerState listener_state() const noexcept
    {
        return listener_ != nullptr ? listener_->state() : ipc::ZmqListenerState::Stopped;
    }

    [[nodiscard]] std::string_view configured_endpoint() const noexcept
    {
        return options_.local_endpoint;
    }

    [[nodiscard]] std::string_view bound_endpoint() const noexcept
    {
        return bound_endpoint_;
    }

    [[nodiscard]] std::string_view last_error_message() const noexcept
    {
        if (!last_error_message_.empty())
        {
            return last_error_message_;
        }

        return listener_ != nullptr ? listener_->last_error_message() : std::string_view{};
    }

    [[nodiscard]] ipc::ZmqListenerMetricsSnapshot metrics() const noexcept
    {
        return listener_ != nullptr ? listener_->metrics() : ipc::ZmqListenerMetricsSnapshot{};
    }

  private:
    void HandleStateChange(ipc::ZmqListenerState state)
    {
        if (listener_ != nullptr)
        {
            bound_endpoint_ = std::string(listener_->bound_endpoint());
        }

        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"state", std::string(ipc::ZmqListenerStateName(state))},
            xs::core::LogContextField{"configuredEndpoint", options_.local_endpoint},
            xs::core::LogContextField{"boundEndpoint", bound_endpoint_},
        };
        LogInfo("Inner network listener state changed.", context);
    }

    void HandleMessage(
        std::vector<std::byte> routing_id,
        std::vector<std::byte> payload)
    {
        const ipc::ZmqListenerMetricsSnapshot snapshot = metrics();
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            xs::core::LogContextField{"receivedMessages", ToString(snapshot.received_message_count)},
            xs::core::LogContextField{"activeConnections", ToString(snapshot.active_connection_count)},
            xs::core::LogContextField{"boundEndpoint", bound_endpoint_},
        };
        LogInfo("Inner network listener received payload.", context);

        if (message_handler_)
        {
            message_handler_(std::move(routing_id), std::move(payload));
        }
    }

    template <std::size_t N>
    void LogInfo(
        std::string_view message,
        const std::array<xs::core::LogContextField, N>& context) const
    {
        logger_.Log(xs::core::LogLevel::Info, "inner-network", message, context);
    }

    xs::core::MainEventLoop& event_loop_;
    xs::core::Logger& logger_;
    InnerNetworkOptions options_{};
    std::string bound_endpoint_{};
    std::string last_error_message_{};
    std::unique_ptr<ipc::ZmqContext> context_{};
    std::unique_ptr<ipc::ZmqPassiveListener> listener_{};
    InnerNetworkMessageHandler message_handler_{};
    bool initialized_{false};
};

InnerNetwork::InnerNetwork(
    xs::core::MainEventLoop& event_loop,
    xs::core::Logger& logger,
    InnerNetworkOptions options)
    : impl_(std::make_unique<Impl>(event_loop, logger, std::move(options)))
{
}

InnerNetwork::~InnerNetwork() = default;

NodeErrorCode InnerNetwork::Init()
{
    return impl_->Init();
}

NodeErrorCode InnerNetwork::Run()
{
    return impl_->Run();
}

NodeErrorCode InnerNetwork::Uninit()
{
    if (impl_ != nullptr)
    {
        return impl_->Uninit();
    }

    return NodeErrorCode::None;
}

NodeErrorCode InnerNetwork::Send(
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    if (impl_ != nullptr)
    {
        return impl_->Send(routing_id, payload);
    }

    return NodeErrorCode::InvalidArgument;
}

void InnerNetwork::SetMessageHandler(InnerNetworkMessageHandler handler)
{
    if (impl_ != nullptr)
    {
        impl_->SetMessageHandler(std::move(handler));
    }
}

bool InnerNetwork::IsRunning() const noexcept
{
    return impl_ != nullptr && impl_->IsRunning();
}

InnerNetworkMode InnerNetwork::mode() const noexcept
{
    return impl_ != nullptr ? impl_->mode() : InnerNetworkMode::Disabled;
}

ipc::ZmqListenerState InnerNetwork::listener_state() const noexcept
{
    return impl_ != nullptr ? impl_->listener_state() : ipc::ZmqListenerState::Stopped;
}

std::string_view InnerNetwork::configured_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->configured_endpoint() : std::string_view{};
}

std::string_view InnerNetwork::bound_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->bound_endpoint() : std::string_view{};
}

std::string_view InnerNetwork::last_error_message() const noexcept
{
    return impl_ != nullptr ? impl_->last_error_message() : std::string_view{};
}

ipc::ZmqListenerMetricsSnapshot InnerNetwork::metrics() const noexcept
{
    return impl_ != nullptr ? impl_->metrics() : ipc::ZmqListenerMetricsSnapshot{};
}

} // namespace xs::node

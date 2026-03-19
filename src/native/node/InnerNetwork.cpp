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

void ClearError(std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

NodeRuntimeErrorCode SetError(
    NodeRuntimeErrorCode code,
    std::string message,
    std::string* error_message)
{
    if (error_message != nullptr)
    {
        if (message.empty())
        {
            *error_message = std::string(NodeRuntimeErrorMessage(code));
        }
        else
        {
            *error_message = std::move(message);
        }
    }

    return code;
}

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

} // namespace

class InnerNetwork::Impl final
{
  public:
    Impl(xs::core::MainEventLoop& event_loop, xs::core::Logger& logger, InnerNetworkOptions options)
        : event_loop_(event_loop), logger_(logger), options_(std::move(options))
    {
    }

    [[nodiscard]] NodeRuntimeErrorCode Init(std::string* error_message)
    {
        if (initialized_)
        {
            return SetError(
                NodeRuntimeErrorCode::InvalidArgument,
                "InnerNetwork is already initialized.",
                error_message);
        }

        if (options_.mode == InnerNetworkMode::Disabled)
        {
            initialized_ = true;
            ClearError(error_message);
            return NodeRuntimeErrorCode::None;
        }

        if (options_.local_endpoint.empty())
        {
            return SetError(
                NodeRuntimeErrorCode::InvalidArgument,
                "InnerNetwork local_endpoint must not be empty.",
                error_message);
        }

        context_ = std::make_unique<ipc::ZmqContext>();
        if (!context_->IsValid())
        {
            const std::string initialization_error = std::string(context_->initialization_error());
            context_.reset();
            return SetError(
                NodeRuntimeErrorCode::NodeInitFailed,
                "Failed to initialize inner network ZeroMQ context: " + initialization_error,
                error_message);
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
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;
    }

    [[nodiscard]] NodeRuntimeErrorCode Run(std::string* error_message)
    {
        if (!initialized_)
        {
            return SetError(
                NodeRuntimeErrorCode::InvalidArgument,
                "InnerNetwork must be initialized before Run().",
                error_message);
        }

        if (options_.mode == InnerNetworkMode::Disabled)
        {
            ClearError(error_message);
            return NodeRuntimeErrorCode::None;
        }

        if (listener_ == nullptr)
        {
            return SetError(
                NodeRuntimeErrorCode::InvalidArgument,
                "InnerNetwork listener is not configured.",
                error_message);
        }

        if (listener_->IsRunning())
        {
            return SetError(
                NodeRuntimeErrorCode::InvalidArgument,
                "InnerNetwork listener is already running.",
                error_message);
        }

        std::string listener_error;
        const ipc::ZmqSocketErrorCode start_result = listener_->Start(&listener_error);
        if (start_result != ipc::ZmqSocketErrorCode::None)
        {
            return SetError(
                NodeRuntimeErrorCode::NodeRunFailed,
                "Failed to start inner network listener: " + listener_error,
                error_message);
        }

        bound_endpoint_ = std::string(listener_->bound_endpoint());
        const std::array<xs::core::LogContextField, 2> context{
            xs::core::LogContextField{"configuredEndpoint", options_.local_endpoint},
            xs::core::LogContextField{"boundEndpoint", bound_endpoint_},
        };
        LogInfo("Inner network listener started.", context);

        ClearError(error_message);
        return NodeRuntimeErrorCode::None;
    }

    void Uninit() noexcept
    {
        if (!initialized_)
        {
            context_.reset();
            listener_.reset();
            bound_endpoint_.clear();
            return;
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
        initialized_ = false;
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
    std::unique_ptr<ipc::ZmqContext> context_{};
    std::unique_ptr<ipc::ZmqPassiveListener> listener_{};
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

NodeRuntimeErrorCode InnerNetwork::Init(std::string* error_message)
{
    return impl_->Init(error_message);
}

NodeRuntimeErrorCode InnerNetwork::Run(std::string* error_message)
{
    return impl_->Run(error_message);
}

void InnerNetwork::Uninit() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->Uninit();
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

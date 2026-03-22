#include "InnerNetwork.h"

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

        if (options_.mode == InnerNetworkMode::PassiveListener && options_.local_endpoint.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork local_endpoint must not be empty.");
        }

        if (options_.mode == InnerNetworkMode::ActiveConnector && options_.remote_endpoint.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork remote_endpoint must not be empty.");
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

        if (options_.mode == InnerNetworkMode::PassiveListener)
        {
            ipc::ZmqPassiveListenerOptions listener_options;
            listener_options.local_endpoint = options_.local_endpoint;

            listener_ =
                std::make_unique<ipc::ZmqPassiveListener>(event_loop_.context(), *context_, std::move(listener_options));
            listener_->SetStateHandler([this](ipc::ZmqListenerState state) {
                try
                {
                    HandleListenerStateChange(state);
                }
                catch (...)
                {
                }
            });
            listener_->SetMessageHandler([this](std::vector<std::byte> routing_id, std::vector<std::byte> payload) {
                try
                {
                    HandlePassiveMessage(std::move(routing_id), std::move(payload));
                }
                catch (...)
                {
                }
            });
        }
        else if (options_.mode == InnerNetworkMode::ActiveConnector)
        {
            ipc::ZmqActiveConnectorOptions connector_options;
            connector_options.remote_endpoint = options_.remote_endpoint;
            connector_options.routing_id = options_.routing_id;

            connector_ =
                std::make_unique<ipc::ZmqActiveConnector>(event_loop_.context(), *context_, std::move(connector_options));
            connector_->SetStateHandler([this](ipc::ZmqConnectionState state) {
                try
                {
                    HandleConnectionStateChange(state);
                }
                catch (...)
                {
                }
            });
            connector_->SetMessageHandler([this](std::vector<std::byte> payload) {
                try
                {
                    HandleActiveMessage(std::move(payload));
                }
                catch (...)
                {
                }
            });
        }

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

        if (options_.mode == InnerNetworkMode::PassiveListener && listener_ == nullptr)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork listener is not configured.");
        }

        if (options_.mode == InnerNetworkMode::ActiveConnector && connector_ == nullptr)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork connector is not configured.");
        }

        if (options_.mode == InnerNetworkMode::PassiveListener && listener_->IsRunning())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork listener is already running.");
        }

        if (options_.mode == InnerNetworkMode::ActiveConnector && connector_->IsRunning())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork connector is already running.");
        }

        if (options_.mode == InnerNetworkMode::PassiveListener)
        {
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
        }
        else if (options_.mode == InnerNetworkMode::ActiveConnector)
        {
            std::string connector_error;
            const ipc::ZmqSocketErrorCode start_result = connector_->Start(&connector_error);
            if (start_result != ipc::ZmqSocketErrorCode::None)
            {
                return SetError(
                    last_error_message_,
                    NodeErrorCode::NodeRunFailed,
                    "Failed to start inner network connector: " + connector_error);
            }

            const std::array<xs::core::LogContextField, 3> context{
                xs::core::LogContextField{"remoteEndpoint", options_.remote_endpoint},
                xs::core::LogContextField{"localEndpoint", options_.local_endpoint},
                xs::core::LogContextField{"routingId", options_.routing_id},
            };
            LogInfo("Inner network active connector started.", context);
        }

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Uninit() noexcept
    {
        if (!initialized_)
        {
            context_.reset();
            connector_.reset();
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

        if (connector_ != nullptr && connector_->IsRunning())
        {
            const std::string remote_endpoint = options_.remote_endpoint;
            const std::string local_endpoint = options_.local_endpoint;

            try
            {
                const std::array<xs::core::LogContextField, 3> context{
                    xs::core::LogContextField{"remoteEndpoint", remote_endpoint},
                    xs::core::LogContextField{"localEndpoint", local_endpoint},
                    xs::core::LogContextField{"state", std::string(ipc::ZmqConnectionStateName(connector_->state()))},
                };
                LogInfo("Inner network active connector stopping.", context);
            }
            catch (...)
            {
            }

            connector_->Stop();

            try
            {
                const std::array<xs::core::LogContextField, 2> context{
                    xs::core::LogContextField{"remoteEndpoint", remote_endpoint},
                    xs::core::LogContextField{"localEndpoint", local_endpoint},
                };
                LogInfo("Inner network active connector stopped.", context);
            }
            catch (...)
            {
            }
        }

        connector_.reset();
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

        if (options_.mode == InnerNetworkMode::PassiveListener)
        {
            if (listener_ == nullptr)
            {
                return SetError(
                    last_error_message_,
                    NodeErrorCode::InvalidArgument,
                    "InnerNetwork Send() requires a configured passive listener.");
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

        if (options_.mode == InnerNetworkMode::ActiveConnector)
        {
            if (!routing_id.empty())
            {
                return SetError(
                    last_error_message_,
                    NodeErrorCode::InvalidArgument,
                    "InnerNetwork active connector mode does not accept routing IDs.");
            }

            if (connector_ == nullptr)
            {
                return SetError(
                    last_error_message_,
                    NodeErrorCode::InvalidArgument,
                    "InnerNetwork Send() requires a configured active connector.");
            }

            std::string connector_error;
            const ipc::ZmqSocketErrorCode send_result = connector_->Send(payload, &connector_error);
            if (send_result != ipc::ZmqSocketErrorCode::None)
            {
                return SetError(
                    last_error_message_,
                    NodeErrorCode::NodeRunFailed,
                    "Failed to send inner network payload: " + connector_error);
            }

            ClearError(last_error_message_);
            return NodeErrorCode::None;
        }

        return SetError(
            last_error_message_,
            NodeErrorCode::InvalidArgument,
            "InnerNetwork Send() is unavailable while disabled.");
    }

    void SetMessageHandler(InnerNetworkMessageHandler handler)
    {
        message_handler_ = std::move(handler);
    }

    [[nodiscard]] bool IsRunning() const noexcept
    {
        if (listener_ != nullptr)
        {
            return listener_->IsRunning();
        }

        return connector_ != nullptr && connector_->IsRunning();
    }

    [[nodiscard]] InnerNetworkMode mode() const noexcept
    {
        return options_.mode;
    }

    [[nodiscard]] ipc::ZmqListenerState listener_state() const noexcept
    {
        return listener_ != nullptr ? listener_->state() : ipc::ZmqListenerState::Stopped;
    }

    [[nodiscard]] ipc::ZmqConnectionState connection_state() const noexcept
    {
        return connector_ != nullptr ? connector_->state() : ipc::ZmqConnectionState::Stopped;
    }

    [[nodiscard]] std::string_view configured_endpoint() const noexcept
    {
        if (options_.mode == InnerNetworkMode::PassiveListener)
        {
            return options_.local_endpoint;
        }

        if (options_.mode == InnerNetworkMode::ActiveConnector)
        {
            return options_.remote_endpoint;
        }

        return std::string_view{};
    }

    [[nodiscard]] std::string_view local_endpoint() const noexcept
    {
        return options_.local_endpoint;
    }

    [[nodiscard]] std::string_view remote_endpoint() const noexcept
    {
        return options_.remote_endpoint;
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

        if (listener_ != nullptr)
        {
            return listener_->last_error_message();
        }

        return connector_ != nullptr ? connector_->last_error_message() : std::string_view{};
    }

    [[nodiscard]] ipc::ZmqListenerMetricsSnapshot metrics() const noexcept
    {
        return listener_ != nullptr ? listener_->metrics() : ipc::ZmqListenerMetricsSnapshot{};
    }

  private:
    void HandleListenerStateChange(ipc::ZmqListenerState state)
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

    void HandlePassiveMessage(
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

    void HandleConnectionStateChange(ipc::ZmqConnectionState state)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"state", std::string(ipc::ZmqConnectionStateName(state))},
            xs::core::LogContextField{"remoteEndpoint", options_.remote_endpoint},
            xs::core::LogContextField{"localEndpoint", options_.local_endpoint},
            xs::core::LogContextField{"routingId", options_.routing_id},
        };
        LogInfo("Inner network active connector state changed.", context);
    }

    void HandleActiveMessage(std::vector<std::byte> payload)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            xs::core::LogContextField{"remoteEndpoint", options_.remote_endpoint},
            xs::core::LogContextField{
                "state",
                std::string(ipc::ZmqConnectionStateName(connection_state())),
            },
        };
        LogInfo("Inner network active connector received payload.", context);

        if (message_handler_)
        {
            message_handler_({}, std::move(payload));
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
    std::unique_ptr<ipc::ZmqActiveConnector> connector_{};
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

ipc::ZmqConnectionState InnerNetwork::connection_state() const noexcept
{
    return impl_ != nullptr ? impl_->connection_state() : ipc::ZmqConnectionState::Stopped;
}

std::string_view InnerNetwork::configured_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->configured_endpoint() : std::string_view{};
}

std::string_view InnerNetwork::local_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->local_endpoint() : std::string_view{};
}

std::string_view InnerNetwork::remote_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->remote_endpoint() : std::string_view{};
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

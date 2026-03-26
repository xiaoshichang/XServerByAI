#include "InnerNetwork.h"

#include <array>
#include <cstddef>
#include <map>
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

        const NodeErrorCode validation_result = ValidateOptions();
        if (validation_result != NodeErrorCode::None)
        {
            return validation_result;
        }

        if (options_.local_endpoint.empty() && options_.connectors.empty())
        {
            initialized_ = true;
            ClearError(last_error_message_);
            return NodeErrorCode::None;
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

        if (!options_.local_endpoint.empty())
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
                    HandleListenerMessage(std::move(routing_id), std::move(payload));
                }
                catch (...)
                {
                }
            });
        }

        for (const InnerNetworkConnectorOptions& connector_options : options_.connectors)
        {
            ConnectorSlot slot;
            slot.options = connector_options;
            slot.connector = std::make_unique<ipc::ZmqActiveConnector>(
                event_loop_.context(),
                *context_,
                ipc::ZmqActiveConnectorOptions{
                    .remote_endpoint = connector_options.remote_endpoint,
                    .routing_id = connector_options.routing_id,
                });
            slot.connector->SetStateHandler([this, connector_id = slot.options.id](ipc::ZmqConnectionState state) {
                try
                {
                    HandleConnectionStateChange(connector_id, state);
                }
                catch (...)
                {
                }
            });
            slot.connector->SetMessageHandler([this, connector_id = slot.options.id](std::vector<std::byte> payload) {
                try
                {
                    HandleConnectorMessage(connector_id, std::move(payload));
                }
                catch (...)
                {
                }
            });
            connectors_.emplace(slot.options.id, std::move(slot));
        }

        initialized_ = true;
        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode Run()
    {
        return Run(std::span<const std::string_view>{});
    }

    [[nodiscard]] NodeErrorCode Run(std::span<const std::string_view> connector_ids)
    {
        if (!initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork must be initialized before Run().");
        }

        if (listener_ == nullptr && connectors_.empty())
        {
            ClearError(last_error_message_);
            return NodeErrorCode::None;
        }

        bool listener_started = false;
        std::vector<std::string> started_connectors;
        std::vector<std::string> target_connectors;

        if (listener_ != nullptr && !listener_->IsRunning())
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

            listener_started = true;
            bound_endpoint_ = std::string(listener_->bound_endpoint());
            const std::array<xs::core::LogContextField, 2> context{
                xs::core::LogContextField{"configuredEndpoint", options_.local_endpoint},
                xs::core::LogContextField{"boundEndpoint", bound_endpoint_},
            };
            LogInfo("Inner network listener started.", context);
        }

        if (connector_ids.empty())
        {
            target_connectors.reserve(connectors_.size());
            for (const auto& [connector_id, slot] : connectors_)
            {
                (void)slot;
                target_connectors.push_back(connector_id);
            }
        }
        else
        {
            std::map<std::string, bool, std::less<>> requested_connector_ids;
            target_connectors.reserve(connector_ids.size());
            for (std::string_view connector_id : connector_ids)
            {
                if (connector_id.empty())
                {
                    StopStartedComponents(listener_started, started_connectors);
                    return SetError(
                        last_error_message_,
                        NodeErrorCode::InvalidArgument,
                        "InnerNetwork connector ID must not be empty.");
                }

                auto iterator = connectors_.find(connector_id);
                if (iterator == connectors_.end() || iterator->second.connector == nullptr)
                {
                    StopStartedComponents(listener_started, started_connectors);
                    return SetError(
                        last_error_message_,
                        NodeErrorCode::InvalidArgument,
                        "InnerNetwork connector '" + std::string(connector_id) + "' is not configured.");
                }

                const std::string connector_id_text(connector_id);
                if (requested_connector_ids.contains(connector_id_text))
                {
                    continue;
                }

                requested_connector_ids.emplace(connector_id_text, true);
                target_connectors.push_back(std::move(connector_id_text));
            }
        }

        for (const std::string& connector_id : target_connectors)
        {
            auto iterator = connectors_.find(connector_id);
            if (iterator == connectors_.end() || iterator->second.connector == nullptr)
            {
                continue;
            }

            ConnectorSlot& slot = iterator->second;
            if (slot.connector->IsRunning())
            {
                continue;
            }

            std::string connector_error;
            const ipc::ZmqSocketErrorCode start_result = slot.connector->Start(&connector_error);
            if (start_result != ipc::ZmqSocketErrorCode::None)
            {
                StopStartedComponents(listener_started, started_connectors);
                return SetError(
                    last_error_message_,
                    NodeErrorCode::NodeRunFailed,
                    "Failed to start inner network connector '" + connector_id + "': " + connector_error);
            }

            started_connectors.push_back(connector_id);
            const std::array<xs::core::LogContextField, 3> context{
                xs::core::LogContextField{"connectorId", connector_id},
                xs::core::LogContextField{"remoteEndpoint", slot.options.remote_endpoint},
                xs::core::LogContextField{"routingId", slot.options.routing_id},
            };
            LogInfo("Inner network active connector started.", context);
        }

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    [[nodiscard]] NodeErrorCode StartConnector(std::string_view connector_id)
    {
        const std::array<std::string_view, 1> connector_ids{connector_id};
        return Run(connector_ids);
    }

    [[nodiscard]] NodeErrorCode Uninit() noexcept
    {
        if (!initialized_)
        {
            ResetState();
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

        for (auto& [connector_id, slot] : connectors_)
        {
            if (slot.connector == nullptr || !slot.connector->IsRunning())
            {
                continue;
            }

            const std::string remote_endpoint = slot.options.remote_endpoint;
            try
            {
                const std::array<xs::core::LogContextField, 3> context{
                    xs::core::LogContextField{"connectorId", connector_id},
                    xs::core::LogContextField{"remoteEndpoint", remote_endpoint},
                    xs::core::LogContextField{
                        "state",
                        std::string(ipc::ZmqConnectionStateName(slot.connector->state())),
                    },
                };
                LogInfo("Inner network active connector stopping.", context);
            }
            catch (...)
            {
            }

            slot.connector->Stop();

            try
            {
                const std::array<xs::core::LogContextField, 2> context{
                    xs::core::LogContextField{"connectorId", connector_id},
                    xs::core::LogContextField{"remoteEndpoint", remote_endpoint},
                };
                LogInfo("Inner network active connector stopped.", context);
            }
            catch (...)
            {
            }
        }

        ResetState();
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

        if (listener_ == nullptr)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork Send() requires a configured listener.");
        }

        if (routing_id.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork Send() requires a non-empty listener routing ID.");
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

    [[nodiscard]] NodeErrorCode SendToConnector(
        std::string_view connector_id,
        std::span<const std::byte> payload)
    {
        if (!initialized_)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork must be initialized before SendToConnector().");
        }

        if (connector_id.empty())
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork connector ID must not be empty.");
        }

        auto iterator = connectors_.find(connector_id);
        if (iterator == connectors_.end() || iterator->second.connector == nullptr)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::InvalidArgument,
                "InnerNetwork connector '" + std::string(connector_id) + "' is not configured.");
        }

        std::string connector_error;
        const ipc::ZmqSocketErrorCode send_result = iterator->second.connector->Send(payload, &connector_error);
        if (send_result != ipc::ZmqSocketErrorCode::None)
        {
            return SetError(
                last_error_message_,
                NodeErrorCode::NodeRunFailed,
                "Failed to send inner network payload through connector '" + std::string(connector_id) +
                    "': " + connector_error);
        }

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    void SetListenerMessageHandler(InnerNetworkListenerMessageHandler handler)
    {
        listener_message_handler_ = std::move(handler);
    }

    void SetConnectorMessageHandler(InnerNetworkConnectorMessageHandler handler)
    {
        connector_message_handler_ = std::move(handler);
    }

    void SetConnectorStateHandler(InnerNetworkConnectionStateHandler handler)
    {
        connector_state_handler_ = std::move(handler);
    }

    [[nodiscard]] bool IsRunning() const noexcept
    {
        if (listener_ != nullptr && listener_->IsRunning())
        {
            return true;
        }

        for (const auto& [connector_id, slot] : connectors_)
        {
            (void)connector_id;
            if (slot.connector != nullptr && slot.connector->IsRunning())
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] bool HasListener() const noexcept
    {
        return listener_ != nullptr;
    }

    [[nodiscard]] std::size_t connector_count() const noexcept
    {
        return connectors_.size();
    }

    [[nodiscard]] ipc::ZmqListenerState listener_state() const noexcept
    {
        return listener_ != nullptr ? listener_->state() : ipc::ZmqListenerState::Stopped;
    }

    [[nodiscard]] ipc::ZmqConnectionState connection_state(std::string_view connector_id) const noexcept
    {
        const auto iterator = connectors_.find(connector_id);
        if (iterator == connectors_.end() || iterator->second.connector == nullptr)
        {
            return ipc::ZmqConnectionState::Stopped;
        }

        return iterator->second.connector->state();
    }

    [[nodiscard]] std::string_view local_endpoint() const noexcept
    {
        return options_.local_endpoint;
    }

    [[nodiscard]] std::string_view remote_endpoint(std::string_view connector_id) const noexcept
    {
        const auto iterator = connectors_.find(connector_id);
        if (iterator == connectors_.end())
        {
            return std::string_view{};
        }

        return iterator->second.options.remote_endpoint;
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

        if (listener_ != nullptr && !listener_->last_error_message().empty())
        {
            return listener_->last_error_message();
        }

        for (const auto& [connector_id, slot] : connectors_)
        {
            (void)connector_id;
            if (slot.connector != nullptr && !slot.connector->last_error_message().empty())
            {
                return slot.connector->last_error_message();
            }
        }

        return std::string_view{};
    }

    [[nodiscard]] ipc::ZmqListenerMetricsSnapshot metrics() const noexcept
    {
        return listener_ != nullptr ? listener_->metrics() : ipc::ZmqListenerMetricsSnapshot{};
    }

  private:
    struct ConnectorSlot
    {
        InnerNetworkConnectorOptions options{};
        std::unique_ptr<ipc::ZmqActiveConnector> connector{};
    };

    [[nodiscard]] NodeErrorCode ValidateOptions()
    {
        std::map<std::string, bool, std::less<>> connector_ids;
        for (const InnerNetworkConnectorOptions& connector : options_.connectors)
        {
            if (connector.id.empty())
            {
                return SetError(
                    last_error_message_,
                    NodeErrorCode::InvalidArgument,
                    "InnerNetwork connector id must not be empty.");
            }

            if (connector.remote_endpoint.empty())
            {
                return SetError(
                    last_error_message_,
                    NodeErrorCode::InvalidArgument,
                    "InnerNetwork connector remote_endpoint must not be empty.");
            }

            if (connector_ids.contains(connector.id))
            {
                return SetError(
                    last_error_message_,
                    NodeErrorCode::InvalidArgument,
                    "InnerNetwork connector id '" + connector.id + "' is duplicated.");
            }

            connector_ids.emplace(connector.id, true);
        }

        ClearError(last_error_message_);
        return NodeErrorCode::None;
    }

    void StopStartedComponents(
        bool listener_started,
        const std::vector<std::string>& started_connectors) noexcept
    {
        for (auto iterator = started_connectors.rbegin(); iterator != started_connectors.rend(); ++iterator)
        {
            auto connector_iterator = connectors_.find(*iterator);
            if (connector_iterator != connectors_.end() && connector_iterator->second.connector != nullptr)
            {
                connector_iterator->second.connector->Stop();
            }
        }

        if (listener_started && listener_ != nullptr)
        {
            listener_->Stop();
        }
    }

    void ResetState() noexcept
    {
        connectors_.clear();
        listener_.reset();
        context_.reset();
        bound_endpoint_.clear();
        listener_message_handler_ = {};
        connector_message_handler_ = {};
        connector_state_handler_ = {};
        initialized_ = false;
        ClearError(last_error_message_);
    }

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

    void HandleListenerMessage(
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

        if (listener_message_handler_)
        {
            listener_message_handler_(std::move(routing_id), std::move(payload));
        }
    }

    void HandleConnectionStateChange(std::string_view connector_id, ipc::ZmqConnectionState state)
    {
        const auto iterator = connectors_.find(connector_id);
        const std::string remote_endpoint =
            iterator != connectors_.end() ? iterator->second.options.remote_endpoint : std::string{};
        const std::string routing_id =
            iterator != connectors_.end() ? iterator->second.options.routing_id : std::string{};
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"connectorId", std::string(connector_id)},
            xs::core::LogContextField{"state", std::string(ipc::ZmqConnectionStateName(state))},
            xs::core::LogContextField{"remoteEndpoint", remote_endpoint},
            xs::core::LogContextField{"routingId", routing_id},
        };
        LogInfo("Inner network active connector state changed.", context);

        if (connector_state_handler_)
        {
            connector_state_handler_(connector_id, state);
        }
    }

    void HandleConnectorMessage(std::string_view connector_id, std::vector<std::byte> payload)
    {
        const auto iterator = connectors_.find(connector_id);
        const std::string remote_endpoint =
            iterator != connectors_.end() ? iterator->second.options.remote_endpoint : std::string{};
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"connectorId", std::string(connector_id)},
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            xs::core::LogContextField{"remoteEndpoint", remote_endpoint},
            xs::core::LogContextField{
                "state",
                std::string(ipc::ZmqConnectionStateName(connection_state(connector_id))),
            },
        };
        LogInfo("Inner network active connector received payload.", context);

        if (connector_message_handler_)
        {
            connector_message_handler_(connector_id, std::move(payload));
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
    std::map<std::string, ConnectorSlot, std::less<>> connectors_{};
    InnerNetworkListenerMessageHandler listener_message_handler_{};
    InnerNetworkConnectorMessageHandler connector_message_handler_{};
    InnerNetworkConnectionStateHandler connector_state_handler_{};
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

NodeErrorCode InnerNetwork::Run(std::span<const std::string_view> connector_ids)
{
    if (impl_ != nullptr)
    {
        return impl_->Run(connector_ids);
    }

    return NodeErrorCode::InvalidArgument;
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

NodeErrorCode InnerNetwork::SendToConnector(
    std::string_view connector_id,
    std::span<const std::byte> payload)
{
    if (impl_ != nullptr)
    {
        return impl_->SendToConnector(connector_id, payload);
    }

    return NodeErrorCode::InvalidArgument;
}

NodeErrorCode InnerNetwork::StartConnector(std::string_view connector_id)
{
    if (impl_ != nullptr)
    {
        return impl_->StartConnector(connector_id);
    }

    return NodeErrorCode::InvalidArgument;
}

void InnerNetwork::SetListenerMessageHandler(InnerNetworkListenerMessageHandler handler)
{
    if (impl_ != nullptr)
    {
        impl_->SetListenerMessageHandler(std::move(handler));
    }
}

void InnerNetwork::SetConnectorMessageHandler(InnerNetworkConnectorMessageHandler handler)
{
    if (impl_ != nullptr)
    {
        impl_->SetConnectorMessageHandler(std::move(handler));
    }
}

void InnerNetwork::SetConnectorStateHandler(InnerNetworkConnectionStateHandler handler)
{
    if (impl_ != nullptr)
    {
        impl_->SetConnectorStateHandler(std::move(handler));
    }
}

bool InnerNetwork::IsRunning() const noexcept
{
    return impl_ != nullptr && impl_->IsRunning();
}

bool InnerNetwork::HasListener() const noexcept
{
    return impl_ != nullptr && impl_->HasListener();
}

std::size_t InnerNetwork::connector_count() const noexcept
{
    return impl_ != nullptr ? impl_->connector_count() : 0U;
}

ipc::ZmqListenerState InnerNetwork::listener_state() const noexcept
{
    return impl_ != nullptr ? impl_->listener_state() : ipc::ZmqListenerState::Stopped;
}

ipc::ZmqConnectionState InnerNetwork::connection_state(std::string_view connector_id) const noexcept
{
    return impl_ != nullptr ? impl_->connection_state(connector_id) : ipc::ZmqConnectionState::Stopped;
}

std::string_view InnerNetwork::local_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->local_endpoint() : std::string_view{};
}

std::string_view InnerNetwork::remote_endpoint(std::string_view connector_id) const noexcept
{
    return impl_ != nullptr ? impl_->remote_endpoint(connector_id) : std::string_view{};
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

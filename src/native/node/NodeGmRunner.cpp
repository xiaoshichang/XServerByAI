#include "NodeGmRunner.h"

#include "ZmqContext.h"

#include <array>
#include <cstddef>
#include <initializer_list>
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

std::string BuildTcpEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return "tcp://" + endpoint.host + ":" + std::to_string(endpoint.port);
}

NodeRuntimeErrorCode ResolveGmControlEndpoint(
    const NodeRuntimeContext& context,
    std::string* output,
    std::string* error_message)
{
    if (output == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "GM control endpoint output must not be null.",
            error_message);
    }

    if (context.process_type != xs::core::ProcessType::Gm)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "GM control listener requires process_type = GM.",
            error_message);
    }

    if (!context.node_config.control_listen_endpoint.has_value())
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "GM node configuration must define control.listenEndpoint.",
            error_message);
    }

    const xs::core::EndpointConfig& endpoint = *context.node_config.control_listen_endpoint;
    if (endpoint.host.empty())
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "GM control.listenEndpoint.host must not be empty.",
            error_message);
    }

    if (endpoint.port == 0U)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "GM control.listenEndpoint.port must be greater than zero.",
            error_message);
    }

    *output = BuildTcpEndpoint(endpoint);
    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

} // namespace

class GmControlListener::Impl final
{
  public:
    Impl(core::MainEventLoop& event_loop, core::Logger& logger, std::string local_endpoint)
        : event_loop_(event_loop), logger_(logger), local_endpoint_(std::move(local_endpoint))
    {
    }

    [[nodiscard]] NodeRuntimeErrorCode Start(std::string* error_message)
    {
        if (listener_ != nullptr)
        {
            return SetError(
                NodeRuntimeErrorCode::InvalidArgument,
                "GM control listener is already running.",
                error_message);
        }

        if (local_endpoint_.empty())
        {
            return SetError(
                NodeRuntimeErrorCode::InvalidArgument,
                "GM control listener local_endpoint must not be empty.",
                error_message);
        }

        context_ = std::make_unique<ipc::ZmqContext>();
        if (!context_->IsValid())
        {
            const std::string initialization_error = std::string(context_->initialization_error());
            context_.reset();
            return SetError(
                NodeRuntimeErrorCode::RoleRunnerFailed,
                "Failed to initialize GM control ZeroMQ context: " + initialization_error,
                error_message);
        }

        ipc::ZmqPassiveListenerOptions options;
        options.local_endpoint = local_endpoint_;

        listener_ = std::make_unique<ipc::ZmqPassiveListener>(event_loop_.context(), *context_, std::move(options));
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

        std::string listener_error;
        const ipc::ZmqSocketErrorCode start_result = listener_->Start(&listener_error);
        if (start_result != ipc::ZmqSocketErrorCode::None)
        {
            const std::string failure_message = "Failed to start GM control listener: " + listener_error;
            listener_.reset();
            context_.reset();
            bound_endpoint_.clear();
            return SetError(NodeRuntimeErrorCode::RoleRunnerFailed, failure_message, error_message);
        }

        bound_endpoint_ = std::string(listener_->bound_endpoint());
        const std::array<core::LogContextField, 2> context{
            core::LogContextField{"configuredEndpoint", local_endpoint_},
            core::LogContextField{"boundEndpoint", bound_endpoint_},
        };
        LogInfo("GM control listener started.", context);

        ClearError(error_message);
        return NodeRuntimeErrorCode::None;
    }

    void Stop() noexcept
    {
        if (listener_ == nullptr)
        {
            context_.reset();
            bound_endpoint_.clear();
            return;
        }

        const ipc::ZmqListenerMetricsSnapshot snapshot = listener_->metrics();
        const std::string bound_endpoint = std::string(listener_->bound_endpoint());
        const std::string configured_endpoint = local_endpoint_;

        try
        {
            const std::array<core::LogContextField, 4> context{
                core::LogContextField{"configuredEndpoint", configured_endpoint},
                core::LogContextField{"boundEndpoint", bound_endpoint},
                core::LogContextField{"receivedMessages", ToString(snapshot.received_message_count)},
                core::LogContextField{"activeConnections", ToString(snapshot.active_connection_count)},
            };
            LogInfo("GM control listener stopping.", context);
        }
        catch (...)
        {
        }

        listener_->Stop();
        listener_.reset();
        context_.reset();
        bound_endpoint_.clear();

        try
        {
            const std::array<core::LogContextField, 2> context{
                core::LogContextField{"configuredEndpoint", configured_endpoint},
                core::LogContextField{"boundEndpoint", bound_endpoint},
            };
            LogInfo("GM control listener stopped.", context);
        }
        catch (...)
        {
        }
    }

    [[nodiscard]] bool IsRunning() const noexcept
    {
        return listener_ != nullptr && listener_->IsRunning();
    }

    [[nodiscard]] ipc::ZmqListenerState listener_state() const noexcept
    {
        return listener_ != nullptr ? listener_->state() : ipc::ZmqListenerState::Stopped;
    }

    [[nodiscard]] std::string_view configured_endpoint() const noexcept
    {
        return local_endpoint_;
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

        const std::array<core::LogContextField, 3> context{
            core::LogContextField{"state", std::string(ipc::ZmqListenerStateName(state))},
            core::LogContextField{"configuredEndpoint", local_endpoint_},
            core::LogContextField{"boundEndpoint", bound_endpoint_},
        };
        LogInfo("GM control listener state changed.", context);
    }

    void HandleMessage(
        std::vector<std::byte> routing_id,
        std::vector<std::byte> payload)
    {
        const ipc::ZmqListenerMetricsSnapshot snapshot = metrics();
        const std::array<core::LogContextField, 5> context{
            core::LogContextField{"routingIdBytes", ToString(static_cast<std::uint64_t>(routing_id.size()))},
            core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            core::LogContextField{"receivedMessages", ToString(snapshot.received_message_count)},
            core::LogContextField{"activeConnections", ToString(snapshot.active_connection_count)},
            core::LogContextField{"boundEndpoint", bound_endpoint_},
        };
        LogInfo("GM control listener received control-plane payload.", context);
    }

    template <std::size_t N>
    void LogInfo(
        std::string_view message,
        const std::array<core::LogContextField, N>& context) const
    {
        logger_.Log(core::LogLevel::Info, "gm-control", message, context);
    }

    core::MainEventLoop& event_loop_;
    core::Logger& logger_;
    std::string local_endpoint_{};
    std::string bound_endpoint_{};
    std::unique_ptr<ipc::ZmqContext> context_{};
    std::unique_ptr<ipc::ZmqPassiveListener> listener_{};
};

GmControlListener::GmControlListener(
    core::MainEventLoop& event_loop,
    core::Logger& logger,
    std::string local_endpoint)
    : impl_(std::make_unique<Impl>(event_loop, logger, std::move(local_endpoint)))
{
}

GmControlListener::~GmControlListener() = default;

NodeRuntimeErrorCode GmControlListener::Start(std::string* error_message)
{
    return impl_->Start(error_message);
}

void GmControlListener::Stop() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->Stop();
    }
}

bool GmControlListener::IsRunning() const noexcept
{
    return impl_ != nullptr && impl_->IsRunning();
}

ipc::ZmqListenerState GmControlListener::listener_state() const noexcept
{
    return impl_ != nullptr ? impl_->listener_state() : ipc::ZmqListenerState::Stopped;
}

std::string_view GmControlListener::configured_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->configured_endpoint() : std::string_view{};
}

std::string_view GmControlListener::bound_endpoint() const noexcept
{
    return impl_ != nullptr ? impl_->bound_endpoint() : std::string_view{};
}

std::string_view GmControlListener::last_error_message() const noexcept
{
    return impl_ != nullptr ? impl_->last_error_message() : std::string_view{};
}

ipc::ZmqListenerMetricsSnapshot GmControlListener::metrics() const noexcept
{
    return impl_ != nullptr ? impl_->metrics() : ipc::ZmqListenerMetricsSnapshot{};
}

NodeRuntimeErrorCode RunGmNode(
    const NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    NodeRoleRuntimeBindings* runtime_bindings,
    std::string* error_message)
{
    if (runtime_bindings == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "GM runtime bindings must not be null.",
            error_message);
    }

    std::string local_endpoint;
    const NodeRuntimeErrorCode endpoint_result = ResolveGmControlEndpoint(context, &local_endpoint, error_message);
    if (endpoint_result != NodeRuntimeErrorCode::None)
    {
        return endpoint_result;
    }

    auto control_listener = std::make_shared<GmControlListener>(event_loop, logger, std::move(local_endpoint));
    const NodeRuntimeErrorCode start_result = control_listener->Start(error_message);
    if (start_result != NodeRuntimeErrorCode::None)
    {
        return start_result;
    }

    runtime_bindings->on_stop = [control_listener](core::MainEventLoop&) {
        control_listener->Stop();
    };

    const std::array<core::LogContextField, 3> runtime_context{
        core::LogContextField{"selector", context.selector},
        core::LogContextField{"nodeId", context.node_id},
        core::LogContextField{"controlEndpoint", std::string(control_listener->bound_endpoint())},
    };
    logger.Log(core::LogLevel::Info, "runtime", "GM runtime entered control-listening state.", runtime_context);

    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

} // namespace xs::node

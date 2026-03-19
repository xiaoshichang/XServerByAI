#include "GmNode.h"

#include <array>
#include <string>

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
            "GM node requires process_type = GM.",
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

} // namespace

GmNode::GmNode(ServerNodeEnvironment environment)
    : ServerNode(environment)
{
}

GmNode::~GmNode() = default;

NodeRuntimeErrorCode GmNode::Init(std::string* error_message)
{
    if (initialized_)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "GM node is already initialized.",
            error_message);
    }

    std::string local_endpoint;
    const NodeRuntimeErrorCode endpoint_result = ResolveGmControlEndpoint(context(), &local_endpoint, error_message);
    if (endpoint_result != NodeRuntimeErrorCode::None)
    {
        return endpoint_result;
    }

    InnerNetworkOptions options;
    options.mode = InnerNetworkMode::PassiveListener;
    options.local_endpoint = std::move(local_endpoint);

    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), std::move(options));
    const NodeRuntimeErrorCode init_result = inner_network_->Init(error_message);
    if (init_result != NodeRuntimeErrorCode::None)
    {
        inner_network_.reset();
        return init_result;
    }

    initialized_ = true;
    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

NodeRuntimeErrorCode GmNode::Run(std::string* error_message)
{
    if (!initialized_ || inner_network_ == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "GM node must be initialized before Run().",
            error_message);
    }

    const NodeRuntimeErrorCode run_result = inner_network_->Run(error_message);
    if (run_result != NodeRuntimeErrorCode::None)
    {
        return run_result;
    }

    const std::array<xs::core::LogContextField, 3> runtime_context{
        xs::core::LogContextField{"selector", context().selector},
        xs::core::LogContextField{"nodeId", context().node_id},
        xs::core::LogContextField{"controlEndpoint", std::string(inner_network_->bound_endpoint())},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "GM node entered control-listening state.", runtime_context);

    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

void GmNode::Uninit() noexcept
{
    if (inner_network_ != nullptr)
    {
        inner_network_->Uninit();
        inner_network_.reset();
    }

    initialized_ = false;
}

} // namespace xs::node

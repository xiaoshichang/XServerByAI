#include "GmNode.h"

#include <array>
#include <string>
#include <utility>

namespace xs::node
{
namespace
{

std::string BuildTcpEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return "tcp://" + endpoint.host + ":" + std::to_string(endpoint.port);
}

} // namespace

GmNode::GmNode(NodeCommandLineArgs args)
    : ServerNode(std::move(args))
{
}

GmNode::~GmNode() = default;

xs::core::ProcessType GmNode::role_process_type() const noexcept
{
    return xs::core::ProcessType::Gm;
}

NodeErrorCode GmNode::OnInit()
{
    if (!node_config().control_listen_endpoint.has_value())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM node configuration must define control.listenEndpoint.");
    }

    const xs::core::EndpointConfig& endpoint = *node_config().control_listen_endpoint;
    if (endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM control.listenEndpoint.host must not be empty.");
    }

    if (endpoint.port == 0U)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM control.listenEndpoint.port must be greater than zero.");
    }

    InnerNetworkOptions options;
    options.mode = InnerNetworkMode::PassiveListener;
    options.local_endpoint = BuildTcpEndpoint(endpoint);

    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), std::move(options));
    const NodeErrorCode init_result = inner_network_->Init();
    if (init_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        return SetError(init_result, error_message);
    }

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GmNode::OnRun()
{
    if (inner_network_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "GM node must be initialized before Run().");
    }

    const NodeErrorCode run_result = inner_network_->Run();
    if (run_result != NodeErrorCode::None)
    {
        return SetError(run_result, std::string(inner_network_->last_error_message()));
    }

    const std::array<xs::core::LogContextField, 3> runtime_context{
        xs::core::LogContextField{"selector", std::string(selector())},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"controlEndpoint", std::string(inner_network_->bound_endpoint())},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "GM node entered control-listening state.", runtime_context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GmNode::OnUninit()
{
    if (inner_network_ != nullptr)
    {
        const NodeErrorCode result = inner_network_->Uninit();
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }

    ClearError();
    return NodeErrorCode::None;
}

} // namespace xs::node

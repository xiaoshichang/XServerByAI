#include "GateNode.h"

#include <string>
#include <utility>

namespace xs::node
{

GateNode::GateNode(NodeCommandLineArgs args)
    : ServerNode(std::move(args))
{
}

GateNode::~GateNode() = default;

xs::core::ProcessType GateNode::role_process_type() const noexcept
{
    return xs::core::ProcessType::Gate;
}

NodeErrorCode GateNode::OnInit()
{
    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), InnerNetworkOptions{});
    client_network_ = std::make_unique<ClientNetwork>(event_loop(), logger());

    const NodeErrorCode inner_result = inner_network_->Init();
    if (inner_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        client_network_.reset();
        return SetError(inner_result, error_message);
    }

    const NodeErrorCode client_result = client_network_->Init();
    if (client_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(client_network_->last_error_message());
        (void)inner_network_->Uninit();
        inner_network_.reset();
        client_network_.reset();
        return SetError(client_result, error_message);
    }

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GateNode::OnRun()
{
    if (inner_network_ == nullptr || client_network_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Gate node must be initialized before Run().");
    }

    const NodeErrorCode inner_result = inner_network_->Run();
    if (inner_result != NodeErrorCode::None)
    {
        return SetError(inner_result, std::string(inner_network_->last_error_message()));
    }

    const NodeErrorCode client_result = client_network_->Run();
    if (client_result != NodeErrorCode::None)
    {
        return SetError(client_result, std::string(client_network_->last_error_message()));
    }

    const std::string message = "Gate node placeholder started for nodeId '" + std::string(node_id()) + "'.";
    logger().Log(xs::core::LogLevel::Info, "runtime", message);

    event_loop().RequestStop();
    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GateNode::OnUninit()
{
    if (client_network_ != nullptr)
    {
        const NodeErrorCode result = client_network_->Uninit();
        const std::string error_message = std::string(client_network_->last_error_message());
        client_network_.reset();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }

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

#include "GateNode.h"

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

} // namespace

GateNode::GateNode(ServerNodeEnvironment environment)
    : ServerNode(environment)
{
}

GateNode::~GateNode() = default;

NodeRuntimeErrorCode GateNode::Init(std::string* error_message)
{
    if (initialized_)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Gate node is already initialized.",
            error_message);
    }

    if (context().process_type != xs::core::ProcessType::Gate)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Gate node requires process_type = Gate.",
            error_message);
    }

    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), InnerNetworkOptions{});
    client_network_ = std::make_unique<ClientNetwork>(event_loop(), logger());

    const NodeRuntimeErrorCode inner_result = inner_network_->Init(error_message);
    if (inner_result != NodeRuntimeErrorCode::None)
    {
        inner_network_.reset();
        client_network_.reset();
        return inner_result;
    }

    const NodeRuntimeErrorCode client_result = client_network_->Init(error_message);
    if (client_result != NodeRuntimeErrorCode::None)
    {
        inner_network_->Uninit();
        inner_network_.reset();
        client_network_.reset();
        return client_result;
    }

    initialized_ = true;
    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

NodeRuntimeErrorCode GateNode::Run(std::string* error_message)
{
    if (!initialized_ || inner_network_ == nullptr || client_network_ == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Gate node must be initialized before Run().",
            error_message);
    }

    const NodeRuntimeErrorCode inner_result = inner_network_->Run(error_message);
    if (inner_result != NodeRuntimeErrorCode::None)
    {
        return inner_result;
    }

    const NodeRuntimeErrorCode client_result = client_network_->Run(error_message);
    if (client_result != NodeRuntimeErrorCode::None)
    {
        return client_result;
    }

    const std::string message = "Gate node placeholder started for selector '" + context().selector + "'.";
    logger().Log(xs::core::LogLevel::Info, "runtime", message);

    event_loop().RequestStop();
    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

void GateNode::Uninit() noexcept
{
    if (client_network_ != nullptr)
    {
        client_network_->Uninit();
        client_network_.reset();
    }

    if (inner_network_ != nullptr)
    {
        inner_network_->Uninit();
        inner_network_.reset();
    }

    initialized_ = false;
}

} // namespace xs::node

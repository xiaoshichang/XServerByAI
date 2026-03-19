#include "GameNode.h"

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

GameNode::GameNode(ServerNodeEnvironment environment)
    : ServerNode(environment)
{
}

GameNode::~GameNode() = default;

NodeRuntimeErrorCode GameNode::Init(std::string* error_message)
{
    if (initialized_)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Game node is already initialized.",
            error_message);
    }

    if (context().process_type != xs::core::ProcessType::Game)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Game node requires process_type = Game.",
            error_message);
    }

    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), InnerNetworkOptions{});
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

NodeRuntimeErrorCode GameNode::Run(std::string* error_message)
{
    if (!initialized_ || inner_network_ == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Game node must be initialized before Run().",
            error_message);
    }

    const NodeRuntimeErrorCode inner_result = inner_network_->Run(error_message);
    if (inner_result != NodeRuntimeErrorCode::None)
    {
        return inner_result;
    }

    const std::string message = "Game node placeholder started for selector '" + context().selector + "'.";
    logger().Log(xs::core::LogLevel::Info, "runtime", message);

    event_loop().RequestStop();
    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

void GameNode::Uninit() noexcept
{
    if (inner_network_ != nullptr)
    {
        inner_network_->Uninit();
        inner_network_.reset();
    }

    initialized_ = false;
}

} // namespace xs::node

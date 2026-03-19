#include "GameNode.h"

#include <string>
#include <utility>

namespace xs::node
{

GameNode::GameNode(NodeCommandLineArgs args)
    : ServerNode(std::move(args))
{
}

GameNode::~GameNode() = default;

xs::core::ProcessType GameNode::role_process_type() const noexcept
{
    return xs::core::ProcessType::Game;
}

NodeErrorCode GameNode::OnInit()
{
    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), InnerNetworkOptions{});
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

NodeErrorCode GameNode::OnRun()
{
    if (inner_network_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Game node must be initialized before Run().");
    }

    const NodeErrorCode inner_result = inner_network_->Run();
    if (inner_result != NodeErrorCode::None)
    {
        return SetError(inner_result, std::string(inner_network_->last_error_message()));
    }

    const std::string message = "Game node placeholder started for selector '" + std::string(selector()) + "'.";
    logger().Log(xs::core::LogLevel::Info, "runtime", message);

    event_loop().RequestStop();
    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GameNode::OnUninit()
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

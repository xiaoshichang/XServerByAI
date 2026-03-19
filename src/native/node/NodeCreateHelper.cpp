#include "NodeCreateHelper.h"

#include "GameNode.h"
#include "GateNode.h"
#include "GmNode.h"

#include "Config.h"

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace xs::node
{

NodeCreateHelper::NodeCreateHelper(NodeCommandLineArgs args)
    : args_(std::move(args))
{
}

NodeErrorCode NodeCreateHelper::ParseCommandLine(int argc, char* argv[])
{
    if (argc != 3)
    {
        return SetError(NodeErrorCode::InvalidArgumentCount, std::string(NodeUsage()));
    }

    if (argv == nullptr || argv[1] == nullptr || argv[2] == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Node command line arguments must not be null.");
    }

    NodeCommandLineArgs parsed_args;
    parsed_args.config_path = argv[1];
    parsed_args.selector = argv[2];

    if (parsed_args.config_path.empty())
    {
        return SetError(NodeErrorCode::EmptyConfigPath, "configPath must not be empty.");
    }

    if (parsed_args.selector.empty())
    {
        return SetError(NodeErrorCode::EmptySelector, "selector must not be empty.");
    }

    args_ = std::move(parsed_args);
    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode NodeCreateHelper::CreateNode(ServerNodePtr* output)
{
    if (output == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Server node output must not be null.");
    }

    *output = nullptr;

    if (args_.config_path.empty())
    {
        return SetError(NodeErrorCode::EmptyConfigPath, "configPath must not be empty.");
    }

    if (args_.selector.empty())
    {
        return SetError(NodeErrorCode::EmptySelector, "selector must not be empty.");
    }

    const std::optional<xs::core::NodeSelector> parsed_selector = xs::core::ParseNodeSelector(args_.selector);
    if (!parsed_selector.has_value())
    {
        return SetError(
            NodeErrorCode::InvalidSelector,
            "selector must be one of gm, gate<index>, or game<index>.");
    }

    try
    {
        switch (parsed_selector->kind)
        {
        case xs::core::NodeSelectorKind::Gm:
            *output = std::make_unique<GmNode>(args_);
            break;

        case xs::core::NodeSelectorKind::Gate:
            *output = std::make_unique<GateNode>(args_);
            break;

        case xs::core::NodeSelectorKind::Game:
            *output = std::make_unique<GameNode>(args_);
            break;
        }
    }
    catch (const std::exception& exception)
    {
        return SetError(
            NodeErrorCode::NodeCreateFailed,
            std::string("Server node creation threw: ") + exception.what());
    }
    catch (...)
    {
        return SetError(NodeErrorCode::NodeCreateFailed, "Server node creation threw an unknown exception.");
    }

    ClearError();
    return NodeErrorCode::None;
}

const NodeCommandLineArgs& NodeCreateHelper::args() const noexcept
{
    return args_;
}

std::string_view NodeCreateHelper::last_error_message() const noexcept
{
    return last_error_message_;
}

void NodeCreateHelper::ClearError() noexcept
{
    last_error_message_.clear();
}

NodeErrorCode NodeCreateHelper::SetError(NodeErrorCode code, std::string message)
{
    if (message.empty())
    {
        last_error_message_ = std::string(NodeErrorMessage(code));
    }
    else
    {
        last_error_message_ = std::move(message);
    }

    return code;
}

} // namespace xs::node

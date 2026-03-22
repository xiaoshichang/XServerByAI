#include "NodeCreateHelper.h"

#include "GameNode.h"
#include "GateNode.h"
#include "GmNode.h"

#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace xs::node
{
namespace
{

enum class ParsedNodeKind : std::uint8_t
{
    Gm,
    Gate,
    Game,
};

bool HasDigitsSuffix(std::string_view value, std::size_t prefix_length) noexcept
{
    if (value.size() <= prefix_length)
    {
        return false;
    }

    for (std::size_t index = prefix_length; index < value.size(); ++index)
    {
        const char ch = value[index];
        if (ch < '0' || ch > '9')
        {
            return false;
        }
    }

    return true;
}

std::optional<ParsedNodeKind> ParseNodeKind(std::string_view node_id) noexcept
{
    if (node_id == "GM")
    {
        return ParsedNodeKind::Gm;
    }

    if (node_id.starts_with("Gate") && HasDigitsSuffix(node_id, 4U))
    {
        return ParsedNodeKind::Gate;
    }

    if (node_id.starts_with("Game") && HasDigitsSuffix(node_id, 4U))
    {
        return ParsedNodeKind::Game;
    }

    return std::nullopt;
}

} // namespace

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
    parsed_args.node_id = argv[2];

    if (parsed_args.config_path.empty())
    {
        return SetError(NodeErrorCode::EmptyConfigPath, "configPath must not be empty.");
    }

    if (parsed_args.node_id.empty())
    {
        return SetError(NodeErrorCode::EmptyNodeId, "nodeId must not be empty.");
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

    if (args_.node_id.empty())
    {
        return SetError(NodeErrorCode::EmptyNodeId, "nodeId must not be empty.");
    }

    const std::optional<ParsedNodeKind> parsed_node = ParseNodeKind(args_.node_id);
    if (!parsed_node.has_value())
    {
        return SetError(
            NodeErrorCode::InvalidNodeId,
            "nodeId must be one of GM, Gate<index>, or Game<index>.");
    }

    try
    {
        switch (*parsed_node)
        {
        case ParsedNodeKind::Gm:
            *output = std::make_unique<GmNode>(args_);
            break;

        case ParsedNodeKind::Gate:
            *output = std::make_unique<GateNode>(args_);
            break;

        case ParsedNodeKind::Game:
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

#include "NodeCommon.h"

namespace xs::node
{

std::string_view NodeUsage() noexcept
{
    return "Usage: xserver-node <configPath> <GM|GateN|GameN>";
}

std::string_view NodeErrorMessage(NodeErrorCode code) noexcept
{
    switch (code)
    {
    case NodeErrorCode::None:
        return "No error.";
    case NodeErrorCode::InvalidArgument:
        return "Invalid node argument.";
    case NodeErrorCode::InvalidArgumentCount:
        return "xserver-node requires exactly 2 arguments.";
    case NodeErrorCode::EmptyConfigPath:
        return "configPath must not be empty.";
    case NodeErrorCode::EmptyNodeId:
        return "nodeId must not be empty.";
    case NodeErrorCode::InvalidNodeId:
        return "Node ID is invalid.";
    case NodeErrorCode::ConfigLoadFailed:
        return "Failed to load node configuration.";
    case NodeErrorCode::LoggerInitFailed:
        return "Failed to initialize node logger.";
    case NodeErrorCode::NodeCreateFailed:
        return "Failed to create server node.";
    case NodeErrorCode::NodeInitFailed:
        return "Server node initialization failed.";
    case NodeErrorCode::NodeRunFailed:
        return "Server node run failed.";
    case NodeErrorCode::NodeUninitFailed:
        return "Server node uninitialization failed.";
    case NodeErrorCode::EventLoopFailed:
        return "Node event loop failed.";
    case NodeErrorCode::UnsupportedProcessType:
        return "Node does not support the selected process type.";
    }

    return "Unknown node error.";
}

} // namespace xs::node

#include "NodeGateRunner.h"

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

} // namespace

NodeRuntimeErrorCode RunGateNode(
    const NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    std::string* error_message)
{
    ClearError(error_message);

    const std::string message = "Gate placeholder runner started for selector '" + context.selector + "'.";
    logger.Log(core::LogLevel::Info, "runtime", message);

    event_loop.RequestStop();
    return NodeRuntimeErrorCode::None;
}

} // namespace xs::node
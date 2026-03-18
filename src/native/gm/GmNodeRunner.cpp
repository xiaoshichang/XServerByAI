#include "GmNodeRunner.h"

#include <string>

namespace xs::gm
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

node::NodeRuntimeErrorCode RunGmNode(
    const node::NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    std::string* error_message)
{
    ClearError(error_message);

    const std::string message = "GM placeholder runner started for selector '" + context.selector + "'.";
    logger.Log(core::LogLevel::Info, "runtime", message);

    event_loop.RequestStop();
    return node::NodeRuntimeErrorCode::None;
}

} // namespace xs::gm
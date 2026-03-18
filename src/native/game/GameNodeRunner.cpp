#include "GameNodeRunner.h"

#include <string>

namespace xs::game
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

node::NodeRuntimeErrorCode RunGameNode(
    const node::NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    std::string* error_message)
{
    ClearError(error_message);

    const std::string message = "Game placeholder runner started for selector '" + context.selector + "'.";
    logger.Log(core::LogLevel::Info, "runtime", message);

    event_loop.RequestStop();
    return node::NodeRuntimeErrorCode::None;
}

} // namespace xs::game
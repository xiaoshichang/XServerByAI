#pragma once

#include "NodeRuntime.h"

#include <string>

namespace xs::game
{

[[nodiscard]] node::NodeRuntimeErrorCode RunGameNode(
    const node::NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    std::string* error_message);

} // namespace xs::game
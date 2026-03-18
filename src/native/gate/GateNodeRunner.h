#pragma once

#include "NodeRuntime.h"

#include <string>

namespace xs::gate
{

[[nodiscard]] node::NodeRuntimeErrorCode RunGateNode(
    const node::NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    std::string* error_message);

} // namespace xs::gate
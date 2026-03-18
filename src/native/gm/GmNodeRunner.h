#pragma once

#include "NodeRuntime.h"

#include <string>

namespace xs::gm
{

[[nodiscard]] node::NodeRuntimeErrorCode RunGmNode(
    const node::NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    std::string* error_message);

} // namespace xs::gm
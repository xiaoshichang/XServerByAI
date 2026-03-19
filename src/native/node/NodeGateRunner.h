#pragma once

#include "NodeRuntime.h"

#include <string>

namespace xs::node
{

[[nodiscard]] NodeRuntimeErrorCode RunGateNode(
    const NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    NodeRoleRuntimeBindings* runtime_bindings,
    std::string* error_message);

} // namespace xs::node

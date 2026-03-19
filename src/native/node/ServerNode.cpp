#include "ServerNode.h"

namespace xs::node
{

ServerNode::ServerNode(ServerNodeEnvironment environment)
    : context_(environment.context), logger_(environment.logger), event_loop_(environment.event_loop)
{
}

ServerNode::~ServerNode() = default;

const NodeRuntimeContext& ServerNode::context() const noexcept
{
    return context_;
}

xs::core::Logger& ServerNode::logger() const noexcept
{
    return logger_;
}

xs::core::MainEventLoop& ServerNode::event_loop() const noexcept
{
    return event_loop_;
}

} // namespace xs::node

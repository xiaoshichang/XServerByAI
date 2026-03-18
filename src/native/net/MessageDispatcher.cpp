#include "MessageDispatcher.h"

#include <utility>

namespace xs::net
{

std::string_view MessageDispatchErrorMessage(MessageDispatchErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case MessageDispatchErrorCode::None:
        return "Success.";
    case MessageDispatchErrorCode::InvalidMessageId:
        return "Message msgId must not be zero.";
    case MessageDispatchErrorCode::HandlerEmpty:
        return "Message handler must not be empty.";
    case MessageDispatchErrorCode::HandlerAlreadyRegistered:
        return "Message handler already registered for msgId.";
    case MessageDispatchErrorCode::HandlerNotFound:
        return "Message handler not found for msgId.";
    }

    return "Unknown message dispatch error.";
}

MessageDispatchErrorCode MessageDispatcher::RegisterHandler(std::uint32_t msg_id, MessageHandler handler)
{
    if (msg_id == 0u)
    {
        return MessageDispatchErrorCode::InvalidMessageId;
    }

    if (!handler)
    {
        return MessageDispatchErrorCode::HandlerEmpty;
    }

    const auto [iterator, inserted] = handlers_.emplace(msg_id, std::move(handler));
    (void)iterator;
    if (!inserted)
    {
        return MessageDispatchErrorCode::HandlerAlreadyRegistered;
    }

    return MessageDispatchErrorCode::None;
}

MessageDispatchErrorCode MessageDispatcher::UnregisterHandler(std::uint32_t msg_id)
{
    if (msg_id == 0u)
    {
        return MessageDispatchErrorCode::InvalidMessageId;
    }

    const std::size_t erased = handlers_.erase(msg_id);
    if (erased == 0u)
    {
        return MessageDispatchErrorCode::HandlerNotFound;
    }

    return MessageDispatchErrorCode::None;
}

MessageDispatchErrorCode MessageDispatcher::Dispatch(const PacketView& packet) const
{
    if (packet.header.msg_id == 0u)
    {
        return MessageDispatchErrorCode::InvalidMessageId;
    }

    const auto iterator = handlers_.find(packet.header.msg_id);
    if (iterator == handlers_.end())
    {
        return MessageDispatchErrorCode::HandlerNotFound;
    }

    iterator->second(packet);
    return MessageDispatchErrorCode::None;
}

void MessageDispatcher::Clear()
{
    handlers_.clear();
}

bool MessageDispatcher::HasHandler(std::uint32_t msg_id) const
{
    return handlers_.find(msg_id) != handlers_.end();
}

std::size_t MessageDispatcher::handler_count() const noexcept
{
    return handlers_.size();
}

} // namespace xs::net
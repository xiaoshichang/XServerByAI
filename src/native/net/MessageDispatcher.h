#pragma once

#include "message/PacketCodec.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <unordered_map>

namespace xs::net
{

enum class MessageDispatchErrorCode : std::uint8_t
{
    None = 0,
    InvalidMessageId = 1,
    HandlerEmpty = 2,
    HandlerAlreadyRegistered = 3,
    HandlerNotFound = 4,
};

[[nodiscard]] std::string_view MessageDispatchErrorMessage(MessageDispatchErrorCode error_code) noexcept;

using MessageHandler = std::function<void(const PacketView&)>;

class MessageDispatcher final
{
  public:
    [[nodiscard]] MessageDispatchErrorCode RegisterHandler(std::uint32_t msg_id, MessageHandler handler);
    [[nodiscard]] MessageDispatchErrorCode UnregisterHandler(std::uint32_t msg_id);
    [[nodiscard]] MessageDispatchErrorCode Dispatch(const PacketView& packet) const;

    void Clear();

    [[nodiscard]] bool HasHandler(std::uint32_t msg_id) const;
    [[nodiscard]] std::size_t handler_count() const noexcept;

  private:
    std::unordered_map<std::uint32_t, MessageHandler> handlers_{};
};

} // namespace xs::net
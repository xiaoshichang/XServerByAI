#pragma once

#include "ZmqContext.h"
#include "ZmqError.h"
#include "ZmqListenerMetrics.h"

#include <asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xs::ipc
{

enum class ZmqListenerState : std::uint8_t
{
    Stopped = 0,
    Listening = 1,
};

[[nodiscard]] std::string_view ZmqListenerStateName(ZmqListenerState state) noexcept;

using ZmqRoutedMessageHandler = std::function<void(std::vector<std::byte>, std::vector<std::byte>)>;
using ZmqListenerStateHandler = std::function<void(ZmqListenerState)>;

struct ZmqPassiveListenerOptions
{
    std::string local_endpoint{};
    std::chrono::milliseconds poll_interval{std::chrono::milliseconds(1)};
    int send_high_water_mark{1024};
    int receive_high_water_mark{1024};
    int handshake_interval_ms{3000};
};

class ZmqPassiveListener final
{
  public:
    ZmqPassiveListener(asio::io_context& io_context, ZmqContext& context, ZmqPassiveListenerOptions options = {});
    ~ZmqPassiveListener();

    ZmqPassiveListener(const ZmqPassiveListener&) = delete;
    ZmqPassiveListener& operator=(const ZmqPassiveListener&) = delete;
    ZmqPassiveListener(ZmqPassiveListener&&) = delete;
    ZmqPassiveListener& operator=(ZmqPassiveListener&&) = delete;

    [[nodiscard]] ZmqSocketErrorCode Start(std::string* error_message = nullptr);
    void Stop() noexcept;
    [[nodiscard]] ZmqSocketErrorCode Send(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> message,
        std::string* error_message = nullptr);

    void SetMessageHandler(ZmqRoutedMessageHandler handler);
    void SetStateHandler(ZmqListenerStateHandler handler);

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] ZmqListenerState state() const noexcept;
    [[nodiscard]] const ZmqPassiveListenerOptions& options() const noexcept;
    [[nodiscard]] std::string_view bound_endpoint() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;
    [[nodiscard]] ZmqListenerMetricsSnapshot metrics() const noexcept;

  private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace xs::ipc

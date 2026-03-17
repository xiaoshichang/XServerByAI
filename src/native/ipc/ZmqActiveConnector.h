#pragma once

#include "ZmqContext.h"

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

namespace xs::ipc {

enum class ZmqConnectionState : std::uint8_t {
    Stopped = 0,
    Connecting = 1,
    Connected = 2,
    Disconnected = 3,
};

[[nodiscard]] std::string_view ZmqConnectionStateName(ZmqConnectionState state) noexcept;

using ZmqMessageHandler = std::function<void(std::vector<std::byte>)>;
using ZmqStateHandler = std::function<void(ZmqConnectionState)>;

struct ZmqActiveConnectorOptions {
    std::string remote_endpoint{};
    std::string routing_id{};
    std::chrono::milliseconds poll_interval{std::chrono::milliseconds(1)};
    int send_high_water_mark{1024};
    int receive_high_water_mark{1024};
    int reconnect_interval_ms{100};
    int reconnect_interval_max_ms{1000};
    int handshake_interval_ms{3000};
};

class ZmqActiveConnector final {
public:
    ZmqActiveConnector(asio::io_context& io_context, ZmqContext& context, ZmqActiveConnectorOptions options = {});
    ~ZmqActiveConnector();

    ZmqActiveConnector(const ZmqActiveConnector&) = delete;
    ZmqActiveConnector& operator=(const ZmqActiveConnector&) = delete;
    ZmqActiveConnector(ZmqActiveConnector&&) = delete;
    ZmqActiveConnector& operator=(ZmqActiveConnector&&) = delete;

    [[nodiscard]] bool Start(std::string* error_message = nullptr);
    void Stop() noexcept;
    [[nodiscard]] bool Send(std::span<const std::byte> message, std::string* error_message = nullptr);

    void SetMessageHandler(ZmqMessageHandler handler);
    void SetStateHandler(ZmqStateHandler handler);

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] ZmqConnectionState state() const noexcept;
    [[nodiscard]] const ZmqActiveConnectorOptions& options() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace xs::ipc

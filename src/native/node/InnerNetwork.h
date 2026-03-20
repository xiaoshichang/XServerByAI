#pragma once

#include "NodeCommon.h"

#include "Logging.h"
#include "MainEventLoop.h"
#include "ZmqListenerMetrics.h"
#include "ZmqPassiveListener.h"

#include <memory>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace xs::node
{

enum class InnerNetworkMode : std::uint8_t
{
    Disabled = 0,
    PassiveListener,
};

struct InnerNetworkOptions
{
    InnerNetworkMode mode{InnerNetworkMode::Disabled};
    std::string local_endpoint{};
};

using InnerNetworkMessageHandler = std::function<void(std::span<const std::byte>, std::span<const std::byte>)>;

class InnerNetwork final
{
  public:
    InnerNetwork(
        xs::core::MainEventLoop& event_loop,
        xs::core::Logger& logger,
        InnerNetworkOptions options = {});
    ~InnerNetwork();

    InnerNetwork(const InnerNetwork&) = delete;
    InnerNetwork& operator=(const InnerNetwork&) = delete;
    InnerNetwork(InnerNetwork&&) = delete;
    InnerNetwork& operator=(InnerNetwork&&) = delete;

    [[nodiscard]] NodeErrorCode Init();
    [[nodiscard]] NodeErrorCode Run();
    [[nodiscard]] NodeErrorCode Uninit();
    [[nodiscard]] NodeErrorCode Send(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void SetMessageHandler(InnerNetworkMessageHandler handler);

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] InnerNetworkMode mode() const noexcept;
    [[nodiscard]] ipc::ZmqListenerState listener_state() const noexcept;
    [[nodiscard]] std::string_view configured_endpoint() const noexcept;
    [[nodiscard]] std::string_view bound_endpoint() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;
    [[nodiscard]] ipc::ZmqListenerMetricsSnapshot metrics() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::node

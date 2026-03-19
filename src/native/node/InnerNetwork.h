#pragma once

#include "NodeRuntime.h"
#include "ZmqListenerMetrics.h"
#include "ZmqPassiveListener.h"

#include <memory>
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

    [[nodiscard]] NodeRuntimeErrorCode Init(std::string* error_message = nullptr);
    [[nodiscard]] NodeRuntimeErrorCode Run(std::string* error_message = nullptr);
    void Uninit() noexcept;

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

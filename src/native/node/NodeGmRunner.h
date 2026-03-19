#pragma once

#include "NodeRuntime.h"
#include "ZmqListenerMetrics.h"
#include "ZmqPassiveListener.h"

#include <memory>
#include <string>
#include <string_view>

namespace xs::node
{

class GmControlListener final
{
  public:
    GmControlListener(
        core::MainEventLoop& event_loop,
        core::Logger& logger,
        std::string local_endpoint);
    ~GmControlListener();

    GmControlListener(const GmControlListener&) = delete;
    GmControlListener& operator=(const GmControlListener&) = delete;
    GmControlListener(GmControlListener&&) = delete;
    GmControlListener& operator=(GmControlListener&&) = delete;

    [[nodiscard]] NodeRuntimeErrorCode Start(std::string* error_message = nullptr);
    void Stop() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] ipc::ZmqListenerState listener_state() const noexcept;
    [[nodiscard]] std::string_view configured_endpoint() const noexcept;
    [[nodiscard]] std::string_view bound_endpoint() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;
    [[nodiscard]] ipc::ZmqListenerMetricsSnapshot metrics() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] NodeRuntimeErrorCode RunGmNode(
    const NodeRuntimeContext& context,
    core::Logger& logger,
    core::MainEventLoop& event_loop,
    NodeRoleRuntimeBindings* runtime_bindings,
    std::string* error_message);

} // namespace xs::node

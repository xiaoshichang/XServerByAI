#pragma once

#include "NodeCommon.h"

#include "Logging.h"
#include "MainEventLoop.h"
#include "ZmqActiveConnector.h"
#include "ZmqListenerMetrics.h"
#include "ZmqPassiveListener.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xs::node
{

struct InnerNetworkConnectorOptions
{
    std::string id{};
    std::string remote_endpoint{};
    std::string routing_id{};
};

struct InnerNetworkOptions
{
    std::string local_endpoint{};
    std::vector<InnerNetworkConnectorOptions> connectors{};
};

using InnerNetworkListenerMessageHandler = std::function<void(std::vector<std::byte>, std::vector<std::byte>)>;
using InnerNetworkConnectorMessageHandler = std::function<void(std::string_view, std::vector<std::byte>)>;
using InnerNetworkConnectionStateHandler = std::function<void(std::string_view, ipc::ZmqConnectionState)>;

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
    [[nodiscard]] NodeErrorCode Run(std::span<const std::string_view> connector_ids);
    [[nodiscard]] NodeErrorCode Uninit();
    [[nodiscard]] NodeErrorCode Send(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    [[nodiscard]] NodeErrorCode SendToConnector(
        std::string_view connector_id,
        std::span<const std::byte> payload);
    [[nodiscard]] NodeErrorCode StartConnector(std::string_view connector_id);
    void SetListenerMessageHandler(InnerNetworkListenerMessageHandler handler);
    void SetConnectorMessageHandler(InnerNetworkConnectorMessageHandler handler);
    void SetConnectorStateHandler(InnerNetworkConnectionStateHandler handler);

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] bool HasListener() const noexcept;
    [[nodiscard]] std::size_t connector_count() const noexcept;
    [[nodiscard]] ipc::ZmqListenerState listener_state() const noexcept;
    [[nodiscard]] ipc::ZmqConnectionState connection_state(std::string_view connector_id) const noexcept;
    [[nodiscard]] std::string_view local_endpoint() const noexcept;
    [[nodiscard]] std::string_view remote_endpoint(std::string_view connector_id) const noexcept;
    [[nodiscard]] std::string_view bound_endpoint() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;
    [[nodiscard]] ipc::ZmqListenerMetricsSnapshot metrics() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::node
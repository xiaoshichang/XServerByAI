#pragma once

#include "InnerNetwork.h"
#include "NodeCommon.h"
#include "ProcessRegistry.h"

#include "Logging.h"
#include "MainEventLoop.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xs::node
{

struct GmInnerServiceOptions
{
    std::uint32_t heartbeat_interval_ms{5000U};
    std::uint32_t heartbeat_timeout_ms{15000U};
    std::chrono::milliseconds timeout_scan_interval{std::chrono::milliseconds(1000)};
    std::chrono::milliseconds invalidated_routing_retention{std::chrono::milliseconds::zero()};
};

class GmInnerService final
{
  public:
    GmInnerService(
        xs::core::MainEventLoop& event_loop,
        xs::core::Logger& logger,
        InnerNetwork& inner_network,
        GmInnerServiceOptions options = {});
    ~GmInnerService();

    GmInnerService(const GmInnerService&) = delete;
    GmInnerService& operator=(const GmInnerService&) = delete;
    GmInnerService(GmInnerService&&) = delete;
    GmInnerService& operator=(GmInnerService&&) = delete;

    [[nodiscard]] NodeErrorCode Init();
    [[nodiscard]] NodeErrorCode Run();
    [[nodiscard]] NodeErrorCode Uninit();

    [[nodiscard]] ProcessRegistry& process_registry() noexcept;
    [[nodiscard]] const ProcessRegistry& process_registry() const noexcept;
    [[nodiscard]] ProcessRegistryErrorCode RegisterProcess(ProcessRegistryRegistration registration);
    [[nodiscard]] ProcessRegistryErrorCode UnregisterProcessByNodeId(std::string_view node_id);
    void HandleInnerMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);

    void InvalidateRoutingId(std::span<const std::byte> routing_id);
    [[nodiscard]] bool ContainsInvalidatedRoutingId(std::span<const std::byte> routing_id) const;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

  private:
    [[nodiscard]] NodeErrorCode SetError(NodeErrorCode code, std::string message = {});
    void ClearError() noexcept;
    [[nodiscard]] std::uint64_t CurrentUnixTimeMilliseconds() const noexcept;
    [[nodiscard]] std::uint64_t InvalidatedRoutingRetentionMs() const noexcept;
    void RememberInvalidatedRoutingId(std::span<const std::byte> routing_id, std::uint64_t now_unix_ms);
    void PruneExpiredInvalidatedRoutingIds(std::uint64_t now_unix_ms);
    void HandleHeartbeatMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleTimeoutScan();
    void Log(
        xs::core::LogLevel level,
        std::string_view message,
        std::span<const xs::core::LogContextField> context = {},
        std::optional<std::int32_t> error_code = std::nullopt,
        std::string_view error_name = {}) const;

    xs::core::MainEventLoop& event_loop_;
    xs::core::Logger& logger_;
    InnerNetwork& inner_network_;
    GmInnerServiceOptions options_{};
    ProcessRegistry process_registry_{};
    std::unordered_map<std::string, std::uint64_t> invalidated_routing_ids_{};
    std::string last_error_message_{};
    xs::core::TimerID timeout_scan_timer_id_{0};
    bool initialized_{false};
    bool running_{false};
};

} // namespace xs::node

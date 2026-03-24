#pragma once

#include "GmControlHttpService.h"
#include "InnerNetworkSessionManager.h"
#include "ServerNode.h"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace xs::node
{

class GmNode final : public ServerNode
{
  public:
    explicit GmNode(NodeCommandLineArgs args);
    ~GmNode() override;

    [[nodiscard]] std::vector<InnerNetworkSession> registry_snapshot() const;

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    void HandleInnerMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleHeartbeatMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleTimeoutScan();
    void RememberInvalidatedRoutingId(
        std::span<const std::byte> routing_id,
        std::uint64_t now_unix_ms);
    void PruneExpiredInvalidatedRoutingIds(std::uint64_t now_unix_ms);
    [[nodiscard]] bool ContainsInvalidatedRoutingId(std::span<const std::byte> routing_id) const;
    [[nodiscard]] std::uint64_t CurrentUnixTimeMilliseconds() const noexcept;
    [[nodiscard]] std::uint64_t InvalidatedRoutingRetentionMs() const noexcept;

    std::unordered_map<std::string, std::uint64_t> invalidated_routing_ids_{};
    xs::core::TimerID timeout_scan_timer_id_{0};
    std::unique_ptr<GmControlHttpService> control_http_service_{};
};

} // namespace xs::node

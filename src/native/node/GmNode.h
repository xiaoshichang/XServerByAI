#pragma once

#include "GmControlHttpService.h"
#include "InnerNetworkSessionManager.h"
#include "ServerNode.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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
    struct ClusterNodesOnlineState final
    {
        std::vector<std::string> expected_gate_node_ids{};
        std::vector<std::string> expected_game_node_ids{};
        std::uint64_t last_server_now_unix_ms{0U};
        bool all_nodes_online{false};
    };

    void HandleInnerMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleRegisterMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleHeartbeatMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleTimeoutScan();
    void InitializeClusterNodesOnlineState();
    [[nodiscard]] bool AreAllExpectedNodesOnline() const noexcept;
    void RefreshClusterNodesOnlineState(std::string_view trigger_node_id = {});
    void SendClusterNodesOnlineNotifyToGame(
        const InnerNetworkSession& session,
        bool all_nodes_online,
        std::uint64_t server_now_unix_ms);

    xs::core::TimerID timeout_scan_timer_id_{0};
    std::unique_ptr<GmControlHttpService> control_http_service_{};
    ClusterNodesOnlineState cluster_nodes_online_state_{};
};

} // namespace xs::node

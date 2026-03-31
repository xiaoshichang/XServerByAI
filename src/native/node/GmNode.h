#pragma once

#include "GmControlHttpService.h"
#include "InnerNetworkSessionManager.h"
#include "ServerNode.h"
#include "message/InnerClusterCodec.h"

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
    enum class ServerStubState : std::uint8_t
    {
        Init = 0,
        Ready,
    };

    struct ClusterNodesOnlineState final
    {
        std::vector<std::string> expected_gate_node_ids{};
        std::vector<std::string> expected_game_node_ids{};
        std::uint64_t last_server_now_unix_ms{0U};
        bool all_nodes_online{false};
    };

    struct GameMeshReadyEntry final
    {
        std::string node_id{};
        bool mesh_ready{false};
        std::uint64_t reported_at_unix_ms{0U};
    };

    struct GameToGateFullConnectionAggregationState final
    {
        std::vector<GameMeshReadyEntry> entries{};
        bool all_expected_games_mesh_ready{false};
    };

    struct ServerStubEntry final
    {
        std::string entity_type{};
        std::string entity_id{"unknown"};
        std::string owner_game_node_id{};
        ServerStubState state{ServerStubState::Init};
    };

    struct ServerStubStateTable final
    {
        std::vector<ServerStubEntry> entries{};
        bool catalog_loaded{false};
        bool catalog_load_failed{false};
    };

    struct ClusterGateOpenState final
    {
        std::uint64_t ready_epoch{0U};
        std::uint64_t last_server_now_unix_ms{0U};
        bool is_open{false};
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
    void HandleGameGateMeshReadyReport(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleGameStubsReadyReport(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleTimeoutScan();
    void InitializeClusterNodesOnlineState();
    void InitializeGameToGateFullConnectionAggregationState();
    void ResetServerStubStateTable() noexcept;
    void ResetServerStubStates() noexcept;
    [[nodiscard]] bool LoadManagedServerStubCatalog();
    [[nodiscard]] bool EnsureServerStubAssignments();
    [[nodiscard]] std::vector<xs::net::ServerStubOwnershipEntry> BuildServerStubOwnershipAssignments() const;
    [[nodiscard]] GmControlHttpStatusSnapshot BuildControlHttpStatusSnapshot() const;
    [[nodiscard]] bool AreAllExpectedNodesOnline() const noexcept;
    [[nodiscard]] bool AreAllExpectedGamesMeshReady() const noexcept;
    [[nodiscard]] bool AreAllServerStubsReady() const noexcept;
    void InvalidateAllGameMeshReadyState();
    void InvalidateGameMeshReadyState(std::string_view game_node_id);
    void RefreshClusterNodesOnlineState(std::string_view trigger_node_id = {});
    void RefreshServerStubDistributeTable();
    void RefreshClusterGateOpenState();
    void SendClusterNodesOnlineNotifyToGame(
        const InnerNetworkSession& session,
        bool all_nodes_online,
        std::uint64_t server_now_unix_ms);
    void SendOwnershipSyncToGame(
        const InnerNetworkSession& session,
        const xs::net::ServerStubOwnershipSync& sync);
    void SendClusterGateOpenNotifyToGate(
        const InnerNetworkSession& session,
        const xs::net::ClusterReadyNotify& notify);
    [[nodiscard]] GameMeshReadyEntry* mesh_ready_entry(std::string_view node_id) noexcept;
    [[nodiscard]] const GameMeshReadyEntry* mesh_ready_entry(std::string_view node_id) const noexcept;

    xs::core::TimerID timeout_scan_timer_id_{0};
    std::unique_ptr<GmControlHttpService> control_http_service_{};
    ClusterNodesOnlineState cluster_nodes_online_state_{};
    GameToGateFullConnectionAggregationState game_to_gate_full_connection_aggregation_state_{};
    ServerStubStateTable server_stub_state_table_{};
    ClusterGateOpenState cluster_gate_open_state_{};
};

} // namespace xs::node

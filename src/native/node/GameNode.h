#pragma once

#include "ServerNode.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace xs::net
{

struct PacketView;

} // namespace xs::net

namespace xs::node
{

class GameNode final : public ServerNode
{
  public:
    explicit GameNode(NodeCommandLineArgs args);
    ~GameNode() override;

    [[nodiscard]] std::string_view managed_assembly_name() const noexcept;
    [[nodiscard]] ipc::ZmqConnectionState inner_connection_state(std::string_view remote_node_id) const noexcept;
    [[nodiscard]] ipc::ZmqConnectionState gm_inner_connection_state() const noexcept;
    [[nodiscard]] bool all_nodes_online() const noexcept;
    [[nodiscard]] std::uint64_t cluster_nodes_online_server_now_unix_ms() const noexcept;

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    struct RuntimeState final
    {
        std::uint64_t started_at_unix_ms{0U};
        std::string managed_assembly_name{"XServer.Managed.GameLogic"};
    };

    void HandleConnectorStateChanged(std::string_view remote_node_id, ipc::ZmqConnectionState state);
    void HandleGmConnectionStateChanged(ipc::ZmqConnectionState state);
    void HandleGateConnectionStateChanged(std::string_view gate_node_id, ipc::ZmqConnectionState state);
    void HandleConnectorMessage(std::string_view remote_node_id, std::span<const std::byte> payload);
    void HandleGmMessage(std::span<const std::byte> payload);
    void HandleGateMessage(std::string_view gate_node_id, std::span<const std::byte> payload);
    void HandleClusterNodesOnlineNotify(const xs::net::PacketView& packet);
    void HandleRegisterResponse(const xs::net::PacketView& packet);
    void HandleHeartbeatResponse(const xs::net::PacketView& packet);
    [[nodiscard]] bool SendRegisterRequest();
    [[nodiscard]] bool SendHeartbeatRequest();
    void ResetRuntimeState() noexcept;
    void ResetGmSessionState();
    void ResetGateSessionStates();
    void StartOrResetHeartbeatTimer(std::uint32_t interval_ms);
    void CancelHeartbeatTimer() noexcept;
    [[nodiscard]] std::uint32_t ConsumeNextInnerSequence() noexcept;
    [[nodiscard]] const xs::core::GameNodeConfig* game_config() const noexcept;
    [[nodiscard]] InnerNetworkSession* remote_session(std::string_view remote_node_id) noexcept;
    [[nodiscard]] const InnerNetworkSession* remote_session(std::string_view remote_node_id) const noexcept;
    [[nodiscard]] InnerNetworkSession* gm_session() noexcept;
    [[nodiscard]] const InnerNetworkSession* gm_session() const noexcept;

    RuntimeState runtime_state_{};
    std::uint64_t last_cluster_nodes_online_server_now_unix_ms_{0U};
    bool all_nodes_online_{false};
};

} // namespace xs::node

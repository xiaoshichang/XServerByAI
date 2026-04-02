#pragma once

#include "ClientNetwork.h"
#include "ServerNode.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace xs::net
{

struct PacketView;

}

namespace xs::node
{

class GateNode final : public ServerNode
{
  public:
    explicit GateNode(NodeCommandLineArgs args);
    ~GateNode() override;

    [[nodiscard]] ipc::ZmqListenerState game_inner_listener_state() const noexcept;
    [[nodiscard]] ipc::ZmqConnectionState inner_connection_state(std::string_view remote_node_id) const noexcept;
    [[nodiscard]] ipc::ZmqConnectionState gm_inner_connection_state() const noexcept;
    [[nodiscard]] bool cluster_ready() const noexcept;
    [[nodiscard]] std::uint64_t cluster_ready_epoch() const noexcept;
    [[nodiscard]] bool client_network_running() const noexcept;

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    struct RuntimeState final
    {
        std::uint64_t started_at_unix_ms{0U};
    };

    void HandleConnectorStateChanged(std::string_view remote_node_id, ipc::ZmqConnectionState state);
    void HandleGmConnectionStateChanged(ipc::ZmqConnectionState state);
    void HandleConnectorMessage(std::string_view remote_node_id, std::span<const std::byte> payload);
    void HandleGmMessage(std::span<const std::byte> payload);
    void HandleGameMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleGameRegisterMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleGameHeartbeatMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleGameForwardStubCallMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleClusterReadyNotify(const xs::net::PacketView& packet);
    void HandleRegisterResponse(const xs::net::PacketView& packet);
    void HandleHeartbeatResponse(const xs::net::PacketView& packet);
    [[nodiscard]] bool SendRegisterRequest();
    [[nodiscard]] bool SendHeartbeatRequest();
    void ResetGmSessionState();
    void ResetGameSessionStates();
    void ResetClusterReadyState();
    void StartOrResetHeartbeatTimer(std::uint32_t interval_ms);
    void CancelHeartbeatTimer() noexcept;
    [[nodiscard]] std::uint32_t ConsumeNextInnerSequence() noexcept;
    [[nodiscard]] const xs::core::GateNodeConfig* gate_config() const noexcept;
    [[nodiscard]] InnerNetworkSession* remote_session(std::string_view remote_node_id) noexcept;
    [[nodiscard]] const InnerNetworkSession* remote_session(std::string_view remote_node_id) const noexcept;
    [[nodiscard]] InnerNetworkSession* gm_session() noexcept;
    [[nodiscard]] const InnerNetworkSession* gm_session() const noexcept;

    std::unique_ptr<ClientNetwork> client_network_{};
    RuntimeState runtime_state_{};
    std::uint64_t cluster_ready_epoch_{0U};
    std::uint64_t last_cluster_ready_server_now_unix_ms_{0U};
    bool cluster_ready_{false};
};

} // namespace xs::node

#pragma once

#include "ClientNetwork.h"
#include "ServerNode.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

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

    [[nodiscard]] std::string_view gm_inner_remote_endpoint() const noexcept;
    [[nodiscard]] std::string_view configured_inner_endpoint() const noexcept;
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
        std::string build_version{"dev"};
        std::vector<std::string> capability_tags{};
    };

    void HandleInnerConnectionStateChanged(ipc::ZmqConnectionState state);
    void HandleInnerMessage(std::span<const std::byte> payload);
    void HandleClusterReadyNotify(const xs::net::PacketView& packet);
    void HandleRegisterResponse(const xs::net::PacketView& packet);
    void HandleHeartbeatResponse(const xs::net::PacketView& packet);
    [[nodiscard]] bool SendRegisterRequest();
    [[nodiscard]] bool SendHeartbeatRequest();
    void ResetGmSessionState();
    void ResetClusterReadyState();
    void StartOrResetHeartbeatTimer(std::uint32_t interval_ms);
    void CancelHeartbeatTimer() noexcept;
    [[nodiscard]] std::uint32_t ConsumeNextInnerSequence() noexcept;
    [[nodiscard]] std::uint64_t CurrentUnixTimeMilliseconds() const noexcept;
    [[nodiscard]] InnerNetworkSession* gm_session() noexcept;
    [[nodiscard]] const InnerNetworkSession* gm_session() const noexcept;

    std::string gm_inner_remote_endpoint_{};
    std::string configured_inner_endpoint_{};
    xs::core::EndpointConfig configured_inner_endpoint_config_{};
    std::unique_ptr<ClientNetwork> client_network_{};
    ipc::ZmqConnectionState gm_inner_connection_state_cache_{ipc::ZmqConnectionState::Stopped};
    RuntimeState runtime_state_{};
    std::uint64_t cluster_ready_epoch_{0U};
    std::uint64_t last_cluster_ready_server_now_unix_ms_{0U};
    bool cluster_ready_{false};
};

} // namespace xs::node

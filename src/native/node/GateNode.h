#pragma once

#include "ClientNetwork.h"
#include "InnerNetwork.h"
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

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    struct GmSessionState final
    {
        std::uint32_t next_seq{1U};
        std::uint32_t register_seq{0U};
        std::uint32_t heartbeat_seq{0U};
        std::uint32_t heartbeat_interval_ms{0U};
        std::uint32_t heartbeat_timeout_ms{0U};
        std::uint64_t started_at_unix_ms{0U};
        std::uint64_t last_server_now_unix_ms{0U};
        xs::core::TimerID heartbeat_timer_id{0};
        std::string build_version{"dev"};
        std::vector<std::string> capability_tags{};
        std::string last_protocol_error{};
        bool registered{false};
        bool register_in_flight{false};
    };

    void HandleInnerConnectionStateChanged(ipc::ZmqConnectionState state);
    void HandleInnerMessage(std::span<const std::byte> payload);
    void HandleRegisterResponse(const xs::net::PacketView& packet);
    void HandleHeartbeatResponse(const xs::net::PacketView& packet);
    [[nodiscard]] bool SendRegisterRequest();
    [[nodiscard]] bool SendHeartbeatRequest();
    void ResetGmSessionState();
    void StartOrResetHeartbeatTimer(std::uint32_t interval_ms);
    void CancelHeartbeatTimer() noexcept;
    [[nodiscard]] std::uint32_t ConsumeNextInnerSequence() noexcept;
    [[nodiscard]] std::uint64_t CurrentUnixTimeMilliseconds() const noexcept;

    std::string gm_inner_remote_endpoint_{};
    std::string configured_inner_endpoint_{};
    xs::core::EndpointConfig configured_inner_endpoint_config_{};
    std::unique_ptr<InnerNetwork> inner_network_{};
    std::unique_ptr<ClientNetwork> client_network_{};
    ipc::ZmqConnectionState gm_inner_connection_state_cache_{ipc::ZmqConnectionState::Stopped};
    GmSessionState gm_session_state_{};
};

} // namespace xs::node

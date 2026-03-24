#pragma once

#include "ServerNode.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

    [[nodiscard]] std::string_view gm_inner_remote_endpoint() const noexcept;
    [[nodiscard]] std::string_view configured_inner_endpoint() const noexcept;
    [[nodiscard]] std::string_view managed_assembly_name() const noexcept;
    [[nodiscard]] ipc::ZmqConnectionState gm_inner_connection_state() const noexcept;

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
        std::string managed_assembly_name{"XServer.Managed.GameLogic"};
        std::vector<std::string> capability_tags{};
    };

    void HandleInnerConnectionStateChanged(ipc::ZmqConnectionState state);
    void HandleInnerMessage(std::span<const std::byte> payload);
    void HandleRegisterResponse(const xs::net::PacketView& packet);
    void HandleHeartbeatResponse(const xs::net::PacketView& packet);
    [[nodiscard]] bool SendRegisterRequest();
    [[nodiscard]] bool SendHeartbeatRequest();
    void ResetRuntimeState() noexcept;
    void ResetGmSessionState();
    void StartOrResetHeartbeatTimer(std::uint32_t interval_ms);
    void CancelHeartbeatTimer() noexcept;
    [[nodiscard]] std::uint32_t ConsumeNextInnerSequence() noexcept;
    [[nodiscard]] std::uint64_t CurrentUnixTimeMilliseconds() const noexcept;
    [[nodiscard]] InnerNetworkSession* gm_session() noexcept;
    [[nodiscard]] const InnerNetworkSession* gm_session() const noexcept;

    std::string gm_inner_remote_endpoint_{};
    std::string configured_inner_endpoint_{};
    xs::core::EndpointConfig configured_inner_endpoint_config_{};
    ipc::ZmqConnectionState gm_inner_connection_state_cache_{ipc::ZmqConnectionState::Stopped};
    RuntimeState runtime_state_{};
};

} // namespace xs::node

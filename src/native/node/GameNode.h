#pragma once

#include "ManagedRuntimeHost.h"
#include "ServerNode.h"
#include "message/InnerClusterCodec.h"
#include "message/PacketCodec.h"
#include "message/RelayCodec.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
    [[nodiscard]] bool mesh_ready() const noexcept;
    [[nodiscard]] std::uint64_t mesh_ready_reported_at_unix_ms() const noexcept;
    [[nodiscard]] std::uint64_t assignment_epoch() const noexcept;
    [[nodiscard]] std::uint64_t ownership_server_now_unix_ms() const noexcept;
    [[nodiscard]] std::vector<xs::net::ServerStubOwnershipEntry> ownership_assignments() const;
    [[nodiscard]] std::vector<xs::net::ServerStubOwnershipEntry> owned_stub_assignments() const;

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override;
    [[nodiscard]] NodeErrorCode OnInit() override;
    [[nodiscard]] NodeErrorCode OnRun() override;
    [[nodiscard]] NodeErrorCode OnUninit() override;

  private:
    struct RuntimeState final
    {
        std::uint64_t started_at_unix_ms{0U};
        std::string managed_assembly_name{"XServer.Managed.Framework"};
    };

    struct MeshReadyState final
    {
        bool current{false};
        std::uint64_t last_reported_at_unix_ms{0U};
    };

    struct OwnershipState final
    {
        std::uint64_t assignment_epoch{0U};
        std::uint64_t server_now_unix_ms{0U};
        std::vector<xs::net::ServerStubOwnershipEntry> assignments{};
        std::vector<xs::net::ServerStubOwnershipEntry> owned_assignments{};
    };

    struct ServiceReadyState final
    {
        std::uint64_t last_reported_assignment_epoch{0U};
        std::uint64_t last_reported_at_unix_ms{0U};
        std::vector<xs::net::ServerStubReadyEntry> ready_entries{};
    };

    void HandleConnectorStateChanged(std::string_view remote_node_id, ipc::ZmqConnectionState state);
    void HandleGmConnectionStateChanged(ipc::ZmqConnectionState state);
    void HandleGateConnectionStateChanged(std::string_view gate_node_id, ipc::ZmqConnectionState state);
    void HandleConnectorMessage(std::string_view remote_node_id, std::span<const std::byte> payload);
    void HandleGmMessage(std::span<const std::byte> payload);
    void HandleGateMessage(std::string_view gate_node_id, std::span<const std::byte> payload);
    static void HandleManagedServerStubReadyCallback(void* context, std::uint64_t assignment_epoch,
                                                     const xs::host::ManagedServerStubReadyEntry* entry);
    static void HandleManagedLogCallback(void* context, std::uint32_t level, const std::uint8_t* category_utf8,
                                         std::uint32_t category_length, const std::uint8_t* message_utf8,
                                         std::uint32_t message_length);
    static std::int64_t HandleManagedCreateOnceTimerCallback(void* context, std::uint64_t delay_ms);
    static std::int32_t HandleManagedCancelTimerCallback(void* context, std::int64_t timer_id);
    static std::int32_t HandleManagedForwardStubCallCallback(
        void* context,
        const std::uint8_t* target_game_node_id_utf8,
        std::uint32_t target_game_node_id_length,
        const std::uint8_t* target_stub_type_utf8,
        std::uint32_t target_stub_type_length,
        std::uint32_t msg_id,
        const std::uint8_t* payload,
        std::uint32_t payload_length);
    void HandleManagedServerStubReady(std::uint64_t assignment_epoch, xs::host::ManagedServerStubReadyEntry entry);
    void HandleManagedLog(std::uint32_t level, std::string_view category, std::string_view message);
    [[nodiscard]] std::int64_t CreateManagedOnceTimer(std::uint64_t delay_ms);
    [[nodiscard]] std::int32_t CancelManagedTimer(std::int64_t timer_id);
    [[nodiscard]] std::int32_t ForwardManagedStubCall(
        std::string_view target_game_node_id,
        std::string_view target_stub_type,
        std::uint32_t msg_id,
        std::span<const std::byte> payload);
    void HandleManagedTimerFired(std::int64_t timer_id);
    void HandleClusterNodesOnlineNotify(const xs::net::PacketView& packet);
    void HandleServerStubOwnershipSync(const xs::net::PacketView& packet);
    void HandleRegisterResponse(const xs::net::PacketView& packet);
    void HandleHeartbeatResponse(const xs::net::PacketView& packet);
    void HandleGateRegisterResponse(std::string_view gate_node_id, const xs::net::PacketView& packet);
    void HandleGateHeartbeatResponse(std::string_view gate_node_id, const xs::net::PacketView& packet);
    void HandleGateCreateAvatarEntity(std::string_view gate_node_id, const xs::net::PacketView& packet);
    [[nodiscard]] bool SendGateAvatarEntityCreateResult(
        std::string_view gate_node_id,
        std::span<const std::byte> request_payload,
        bool success,
        std::string_view error_message);
    [[nodiscard]] bool SendRegisterRequest();
    [[nodiscard]] bool SendHeartbeatRequest();
    void OnAllGateConnected();
    [[nodiscard]] bool SendMeshReadyReport();
    [[nodiscard]] bool SendServiceReadyReport();
    [[nodiscard]] bool SendGateRegisterRequest(std::string_view gate_node_id);
    [[nodiscard]] bool SendGateHeartbeatRequest(std::string_view gate_node_id);
    [[nodiscard]] NodeErrorCode InitializeManagedRuntime(const xs::core::ManagedConfig& managed_config);
    void StartGateConnectors();
    void ResetRuntimeState() noexcept;
    void ResetGateSessionStates();
    void CheckAllLocalStubsReady();
    [[nodiscard]] bool CreateAllLocalStubs(const xs::net::ServerStubOwnershipSync& sync);
    void StartOrResetHeartbeatTimer(std::uint32_t interval_ms);
    void StartOrResetGateHeartbeatTimer(std::string_view gate_node_id, std::uint32_t interval_ms);
    void CancelHeartbeatTimer() noexcept;
    void CancelGateHeartbeatTimer(std::string_view gate_node_id) noexcept;
    [[nodiscard]] bool AreAllGateSessionsConnected() const noexcept;
    [[nodiscard]] std::uint32_t ConsumeNextInnerSequence(InnerNetworkSession* session) noexcept;
    [[nodiscard]] const xs::core::GameNodeConfig* game_config() const noexcept;
    [[nodiscard]] InnerNetworkSession* remote_session(std::string_view remote_node_id) noexcept;
    [[nodiscard]] const InnerNetworkSession* remote_session(std::string_view remote_node_id) const noexcept;
    [[nodiscard]] InnerNetworkSession* gm_session() noexcept;
    [[nodiscard]] const InnerNetworkSession* gm_session() const noexcept;

    RuntimeState runtime_state_{};
    MeshReadyState mesh_ready_state_{};
    OwnershipState ownership_state_{};
    ServiceReadyState service_ready_state_{};
    xs::host::ManagedRuntimeHost managed_runtime_host_{};
    xs::host::ManagedExports managed_exports_{};
    bool managed_exports_loaded_{false};
    std::uint64_t last_cluster_nodes_online_server_now_unix_ms_{0U};
    bool all_nodes_online_{false};
};

} // namespace xs::node

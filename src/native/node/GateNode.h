#pragma once

#include "ClientNetwork.h"
#include "GateAuthHttpService.h"
#include "ServerNode.h"

#include <cstddef>
#include <cstdint>
#include <map>
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
    [[nodiscard]] std::size_t client_network_session_count() const noexcept;

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

    struct ClientConversationReservation final
    {
        std::string account{};
        std::string remote_address{};
        std::uint64_t issued_at_unix_ms{0U};
        std::uint64_t expires_at_unix_ms{0U};
    };

    struct ClientSessionRecord final
    {
        std::uint64_t session_id{0U};
        std::uint32_t conversation{0U};
        std::string account_id{};
        std::string avatar_id{};
        std::string avatar_name{};
        std::string game_node_id{};
        std::string gate_node_id{};
        std::uint32_t pending_select_avatar_seq{0U};
        std::uint64_t authenticated_at_unix_ms{0U};
        std::uint64_t last_active_unix_ms{0U};
        bool closed{false};
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
    void HandleGameAvatarEntityCreateResultMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleGamePushToClientMessage(
        std::span<const std::byte> routing_id,
        std::span<const std::byte> payload);
    void HandleGameForwardProxyCallMessage(
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
    [[nodiscard]] std::uint32_t ConsumeNextClientConversation() noexcept;
    [[nodiscard]] GateAuthLoginResult HandleAuthLogin(const GateAuthLoginRequest& request);
    [[nodiscard]] bool CanOpenClientSession(
        std::uint32_t conversation,
        const xs::net::Endpoint& remote_endpoint,
        std::string* error_message);
    [[nodiscard]] bool HandleClientSessionOpened(
        ClientSession& session,
        std::string* error_message);
    void HandleClientPayloadReceived(
        ClientSession& session,
        std::span<const std::byte> payload);
    [[nodiscard]] bool TryRegisterAuthenticatedClientSession(
        const ClientConversationReservation& reservation,
        ClientSession& session,
        std::string* error_message);
    [[nodiscard]] bool HandleClientSelectAvatarPacket(
        ClientSession& session,
        const xs::net::PacketView& packet,
        std::string* error_message);
    [[nodiscard]] bool HandleClientEntityRpcPacket(
        ClientSession& session,
        const xs::net::PacketView& packet,
        std::string* error_message);
    [[nodiscard]] bool SendClientSelectAvatarResult(
        std::uint64_t session_id,
        std::uint32_t request_seq,
        bool success,
        std::string_view account_id,
        std::string_view avatar_id,
        std::string_view avatar_name,
        std::string_view game_node_id,
        std::string_view error_message,
        std::string* transport_error_message);
    [[nodiscard]] bool SendCreateAvatarEntityRequest(
        const ClientSessionRecord& session_record,
        std::string_view avatar_name,
        std::string* error_message);
    [[nodiscard]] std::string ResolveAvatarGameNodeId() const;
    void ClearClientSessionRecord(std::uint64_t session_id) noexcept;
    [[nodiscard]] ClientSessionRecord* client_session_record(std::uint64_t session_id) noexcept;
    [[nodiscard]] const ClientSessionRecord* client_session_record(std::uint64_t session_id) const noexcept;
    void PruneExpiredClientConversationReservations(std::uint64_t now_unix_ms) noexcept;
    [[nodiscard]] std::string ResolveAdvertisedClientHost(std::string_view remote_address) const;
    [[nodiscard]] const xs::core::GateNodeConfig* gate_config() const noexcept;
    [[nodiscard]] InnerNetworkSession* remote_session(std::string_view remote_node_id) noexcept;
    [[nodiscard]] const InnerNetworkSession* remote_session(std::string_view remote_node_id) const noexcept;
    [[nodiscard]] InnerNetworkSession* gm_session() noexcept;
    [[nodiscard]] const InnerNetworkSession* gm_session() const noexcept;

    std::unique_ptr<ClientNetwork> client_network_{};
    std::unique_ptr<GateAuthHttpService> auth_http_service_{};
    std::map<std::uint32_t, ClientConversationReservation, std::less<>> client_conversation_reservations_{};
    std::map<std::uint64_t, ClientSessionRecord, std::less<>> client_session_records_{};
    std::map<std::string, std::uint64_t, std::less<>> session_ids_by_account_{};
    std::map<std::string, std::uint64_t, std::less<>> session_ids_by_avatar_{};
    RuntimeState runtime_state_{};
    std::uint64_t cluster_ready_epoch_{0U};
    std::uint64_t last_cluster_ready_server_now_unix_ms_{0U};
    std::uint32_t next_client_conversation_{1U};
    bool cluster_ready_{false};
};

} // namespace xs::node

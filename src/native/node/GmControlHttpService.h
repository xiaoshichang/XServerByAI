#pragma once

#include "NodeCommon.h"

#include "Config.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace xs::node
{

struct GmControlHttpResponse
{
    std::uint16_t status_code{200};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body{};
    bool request_stop{false};
};

struct GmControlHttpStatusSnapshot
{
    struct StartupFlowSnapshot final
    {
        std::uint64_t expected_game_count{0};
        std::uint64_t expected_gate_count{0};
        std::uint64_t registered_game_count{0};
        std::uint64_t registered_gate_count{0};
        bool all_nodes_online{false};
        std::uint64_t last_all_nodes_online_server_now_unix_ms{0};
        bool all_expected_games_mesh_ready{false};
        bool reflection_loaded{false};
        bool reflection_load_failed{false};
        bool ownership_active{false};
        std::uint64_t assignment_epoch{0};
        std::uint64_t total_stub_count{0};
        std::uint64_t assigned_stub_count{0};
        std::uint64_t ready_stub_count{0};
        std::uint64_t ready_epoch{0};
        bool cluster_ready{false};
        std::uint64_t last_cluster_ready_server_now_unix_ms{0};
    };

    struct NodeSnapshot final
    {
        std::string node_id{};
        std::string process_type{};
        std::uint32_t pid{0};
        std::string inner_network_endpoint{};
        std::uint64_t last_heartbeat_at_unix_ms{0};
        std::uint64_t last_server_now_unix_ms{0};
        std::string last_protocol_error{};
        bool registered{false};
        bool heartbeat_timed_out{false};
        bool online{false};
        bool inner_network_ready{false};
    };

    struct GameMeshReadySnapshot final
    {
        std::string node_id{};
        bool mesh_ready{false};
        std::uint64_t reported_at_unix_ms{0};
    };

    struct StubSnapshot final
    {
        std::string entity_type{};
        std::string entity_id{};
        std::string state{};
    };

    struct StubOwnerSnapshot final
    {
        std::string node_id{};
        std::uint64_t owned_stub_count{0};
        std::uint64_t ready_stub_count{0};
        std::vector<StubSnapshot> stubs{};
    };

    std::string inner_network_endpoint{};
    std::uint64_t registered_process_count{0};
    bool running{false};
    StartupFlowSnapshot startup_flow{};
    std::vector<NodeSnapshot> nodes{};
    std::vector<GameMeshReadySnapshot> game_mesh_ready{};
    std::vector<StubOwnerSnapshot> stub_distribution{};
};

using GmControlHttpStatusProvider = std::function<GmControlHttpStatusSnapshot()>;
using GmControlHttpStopHandler = std::function<void()>;
using GmControlHttpBoardcaseHandler = std::function<GmControlHttpResponse(std::string_view message)>;

struct GmControlHttpServiceOptions
{
    xs::core::EndpointConfig listen_endpoint{};
    std::string node_id{};
    GmControlHttpStatusProvider status_provider{};
    GmControlHttpStopHandler stop_handler{};
    GmControlHttpBoardcaseHandler boardcase_handler{};
};

class GmControlHttpService final
{
  public:
    GmControlHttpService(
        xs::core::MainEventLoop& event_loop,
        xs::core::Logger& logger,
        GmControlHttpServiceOptions options = {});
    ~GmControlHttpService();

    GmControlHttpService(const GmControlHttpService&) = delete;
    GmControlHttpService& operator=(const GmControlHttpService&) = delete;
    GmControlHttpService(GmControlHttpService&&) = delete;
    GmControlHttpService& operator=(GmControlHttpService&&) = delete;

    [[nodiscard]] NodeErrorCode Init();
    [[nodiscard]] NodeErrorCode Run();
    [[nodiscard]] NodeErrorCode Uninit();

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::string_view configured_endpoint() const noexcept;
    [[nodiscard]] std::string_view bound_endpoint() const noexcept;
    [[nodiscard]] std::string_view last_error_message() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::node

#include "GameNode.h"
#include "GmNode.h"
#include "Json.h"
#include "TestManagedConfigJson.h"
#include "TimeUtils.h"
#include "message/InnerClusterCodec.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RelayCodec.h"
#include "message/RegisterCodec.h"

#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>

#include <zmq.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{

int g_failures = 0;

void Check(bool condition, const char* expression, const char* message = nullptr)
{
    if (condition)
    {
        return;
    }

    std::cerr << "Check failed: " << expression;
    if (message != nullptr)
    {
        std::cerr << " (" << message << ")";
    }
    std::cerr << '\n';
    ++g_failures;
}

#define XS_CHECK(expr) Check((expr), #expr)
#define XS_CHECK_MSG(expr, message) Check((expr), #expr, (message))

bool IsCanonicalGuidText(std::string_view value)
{
    if (value.size() != 36U)
    {
        return false;
    }

    for (std::size_t index = 0U; index < value.size(); ++index)
    {
        if (index == 8U || index == 13U || index == 18U || index == 23U)
        {
            if (value[index] != '-')
            {
                return false;
            }

            continue;
        }

        if (!std::isxdigit(static_cast<unsigned char>(value[index])))
        {
            return false;
        }
    }

    return true;
}

class RawZmqSocket final
{
  public:
    explicit RawZmqSocket(int type)
    {
        context_ = zmq_ctx_new();
        if (context_ == nullptr)
        {
            return;
        }

        socket_ = zmq_socket(context_, type);
        if (socket_ == nullptr)
        {
            return;
        }

        int linger = 0;
        (void)zmq_setsockopt(socket_, ZMQ_LINGER, &linger, sizeof(linger));
    }

    ~RawZmqSocket()
    {
        Close();
    }

    RawZmqSocket(const RawZmqSocket&) = delete;
    RawZmqSocket& operator=(const RawZmqSocket&) = delete;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return context_ != nullptr && socket_ != nullptr;
    }

    [[nodiscard]] void* socket() const noexcept
    {
        return socket_;
    }

    [[nodiscard]] std::string BindLoopbackTcp()
    {
        char endpoint[256]{};
        size_t endpoint_length = sizeof(endpoint);
        if (zmq_bind(socket_, "tcp://127.0.0.1:*") != 0)
        {
            return {};
        }

        if (zmq_getsockopt(socket_, ZMQ_LAST_ENDPOINT, endpoint, &endpoint_length) != 0)
        {
            return {};
        }

        return endpoint;
    }

    void Close() noexcept
    {
        if (socket_ != nullptr)
        {
            (void)zmq_close(socket_);
            socket_ = nullptr;
        }

        if (context_ != nullptr)
        {
            (void)zmq_ctx_shutdown(context_);
            (void)zmq_ctx_term(context_);
            context_ = nullptr;
        }
    }

  private:
    void* context_{nullptr};
    void* socket_{nullptr};
};

std::filesystem::path PrepareTestDirectory(std::string_view name)
{
    const std::filesystem::path path = std::filesystem::current_path() / "test-output" / std::string(name);
    std::error_code error_code;
    std::filesystem::remove_all(path, error_code);
    std::filesystem::create_directories(path, error_code);
    return path;
}

void CleanupTestDirectory(const std::filesystem::path& path)
{
    std::error_code error_code;
    std::filesystem::remove_all(path, error_code);
}

bool WriteJsonFile(const std::filesystem::path& path, const xs::core::Json& value)
{
    std::string error_message;
    const xs::core::JsonErrorCode result = xs::core::SaveJsonFile(path, value, &error_message);
    XS_CHECK_MSG(result == xs::core::JsonErrorCode::None, error_message.c_str());
    return result == xs::core::JsonErrorCode::None;
}

std::uint16_t AcquireLoopbackPort()
{
    asio::io_context io_context;
    asio::ip::tcp::acceptor acceptor(
        io_context,
        asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0u));
    const std::uint16_t port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

xs::core::Json MakeClusterConfigJson(
    const std::filesystem::path& base_path,
    std::uint16_t gm_inner_port,
    std::uint16_t gm_control_port,
    const std::vector<std::uint16_t>& gate_inner_ports = std::vector<std::uint16_t>{7000U},
    std::uint16_t game_inner_port = 7100U,
    std::string_view gate_inner_host = "127.0.0.1")
{
    const std::string root_log_dir = (base_path / "logs").string();
    const std::vector<std::uint16_t> effective_gate_inner_ports =
        gate_inner_ports.empty() ? std::vector<std::uint16_t>{7000U} : gate_inner_ports;

    xs::core::Json gate_block = xs::core::Json::object();
    for (std::size_t index = 0; index < effective_gate_inner_ports.size(); ++index)
    {
        const std::string gate_node_id = "Gate" + std::to_string(index);
        gate_block[gate_node_id] = xs::core::Json{
            {"innerNetwork",
             xs::core::Json{
                 {"listenEndpoint",
                  xs::core::Json{{"host", std::string(gate_inner_host)}, {"port", effective_gate_inner_ports[index]}}},
             }},
            {"clientNetwork",
             xs::core::Json{
                 {"listenEndpoint",
                  xs::core::Json{{"host", "0.0.0.0"}, {"port", static_cast<std::uint16_t>(4000U + index)}}},
             }},
        };
    }

    return xs::core::Json{
        {"env", xs::core::Json{{"id", "local-dev"}, {"environment", "dev"}}},
        {"logging",
         xs::core::Json{
             {"rootDir", root_log_dir},
             {"minLevel", "Info"},
             {"flushIntervalMs", 1000},
             {"rotateDaily", true},
             {"maxFileSizeMB", 64},
             {"maxRetainedFiles", 10},
         }},
        {"kcp",
         xs::core::Json{
             {"mtu", 1200},
             {"sndwnd", 256},
             {"rcvwnd", 128},
             {"nodelay", true},
             {"intervalMs", 10},
             {"fastResend", 2},
             {"noCongestionWindow", false},
             {"minRtoMs", 30},
             {"deadLinkCount", 20},
             {"streamMode", false},
         }},
        {"managed", xs::tests::MakeManagedConfigJson()},
        {"gm",
         xs::core::Json{
             {"innerNetwork",
              xs::core::Json{
                  {"listenEndpoint",
                   xs::core::Json{{"host", "127.0.0.1"}, {"port", gm_inner_port}}},
              }},
             {"controlNetwork",
              xs::core::Json{
                  {"listenEndpoint",
                   xs::core::Json{{"host", "127.0.0.1"}, {"port", gm_control_port}}},
              }},
         }},
        {"gate", gate_block},
        {"game",
         xs::core::Json{
             {"Game0",
              xs::core::Json{
                  {"innerNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "127.0.0.1"}, {"port", game_inner_port}}},
                   }},
              }},
         }},
    };
}

bool WriteRuntimeConfig(
    const std::filesystem::path& base_path,
    std::uint16_t gm_inner_port,
    std::uint16_t gm_control_port,
    std::filesystem::path* file_path,
    const std::vector<std::uint16_t>& gate_inner_ports = std::vector<std::uint16_t>{7000U},
    std::uint16_t game_inner_port = 7100U,
    std::string_view gate_inner_host = "127.0.0.1")
{
    if (file_path == nullptr)
    {
        XS_CHECK(false);
        return false;
    }

    *file_path = base_path / "config.json";
    return WriteJsonFile(
        *file_path,
        MakeClusterConfigJson(
            base_path,
            gm_inner_port,
            gm_control_port,
            gate_inner_ports,
            game_inner_port,
            gate_inner_host));
}

bool DirectoryContainsRegularFile(const std::filesystem::path& path)
{
    std::error_code error_code;
    if (!std::filesystem::exists(path, error_code) || error_code)
    {
        return false;
    }

    std::filesystem::directory_iterator iterator(path, error_code);
    std::filesystem::directory_iterator end;
    if (error_code)
    {
        return false;
    }

    for (; iterator != end; iterator.increment(error_code))
    {
        if (error_code)
        {
            return false;
        }

        if (iterator->is_regular_file(error_code) && !error_code)
        {
            return true;
        }

        error_code.clear();
    }

    return false;
}

std::string ReadDirectoryText(const std::filesystem::path& path)
{
    std::string text;
    std::error_code error_code;
    std::filesystem::directory_iterator iterator(path, error_code);
    std::filesystem::directory_iterator end;
    if (error_code)
    {
        return text;
    }

    for (; iterator != end; iterator.increment(error_code))
    {
        if (error_code)
        {
            break;
        }

        if (!iterator->is_regular_file(error_code) || error_code)
        {
            error_code.clear();
            continue;
        }

        std::ifstream stream(iterator->path(), std::ios::binary);
        if (!stream.is_open())
        {
            continue;
        }

        text.append(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        text.push_back('\n');
    }

    return text;
}

bool WaitUntil(std::chrono::milliseconds timeout, const std::function<bool()>& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return predicate();
}

class RunningGmNode final
{
  public:
    explicit RunningGmNode(const std::filesystem::path& config_path)
        : node_({
              .config_path = config_path,
              .node_id = "GM",
          })
    {
    }

    ~RunningGmNode()
    {
        if (run_thread_.joinable())
        {
            node_.RequestStop();
            run_thread_.join();
        }

        (void)node_.Uninit();
    }

    bool Start()
    {
        const xs::node::NodeErrorCode init_result = node_.Init();
        XS_CHECK_MSG(init_result == xs::node::NodeErrorCode::None, node_.last_error_message().data());
        if (init_result != xs::node::NodeErrorCode::None)
        {
            return false;
        }

        run_thread_ = std::thread([this]() {
            run_result_ = node_.Run();
            run_error_ = std::string(node_.last_error_message());
        });
        return true;
    }

    void StopAndJoin()
    {
        node_.RequestStop();
        if (run_thread_.joinable())
        {
            run_thread_.join();
        }
    }

    bool Uninit()
    {
        const xs::node::NodeErrorCode uninit_result = node_.Uninit();
        XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node_.last_error_message().data());
        return uninit_result == xs::node::NodeErrorCode::None;
    }

    [[nodiscard]] xs::node::GmNode& node() noexcept
    {
        return node_;
    }

    [[nodiscard]] xs::node::NodeErrorCode run_result() const noexcept
    {
        return run_result_;
    }

    [[nodiscard]] std::string_view run_error() const noexcept
    {
        return run_error_;
    }

  private:
    xs::node::GmNode node_;
    std::thread run_thread_{};
    xs::node::NodeErrorCode run_result_{xs::node::NodeErrorCode::None};
    std::string run_error_{};
};

class RunningGameNode final
{
  public:
    explicit RunningGameNode(const std::filesystem::path& config_path)
        : node_({
              .config_path = config_path,
              .node_id = "Game0",
          })
    {
    }

    ~RunningGameNode()
    {
        if (run_thread_.joinable())
        {
            node_.RequestStop();
            run_thread_.join();
        }

        (void)node_.Uninit();
    }

    bool Start()
    {
        const xs::node::NodeErrorCode init_result = node_.Init();
        XS_CHECK_MSG(init_result == xs::node::NodeErrorCode::None, node_.last_error_message().data());
        if (init_result != xs::node::NodeErrorCode::None)
        {
            return false;
        }

        run_thread_ = std::thread([this]() {
            run_result_ = node_.Run();
            run_error_ = std::string(node_.last_error_message());
            run_completed_.store(true);
        });
        return true;
    }

    void StopAndJoin()
    {
        node_.RequestStop();
        if (run_thread_.joinable())
        {
            run_thread_.join();
        }
    }

    bool Uninit()
    {
        const xs::node::NodeErrorCode uninit_result = node_.Uninit();
        XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node_.last_error_message().data());
        return uninit_result == xs::node::NodeErrorCode::None;
    }

    [[nodiscard]] xs::node::GameNode& node() noexcept
    {
        return node_;
    }

    [[nodiscard]] bool run_completed() const noexcept
    {
        return run_completed_.load();
    }

    [[nodiscard]] xs::node::NodeErrorCode run_result() const noexcept
    {
        return run_result_;
    }

    [[nodiscard]] std::string_view run_error() const noexcept
    {
        return run_error_;
    }

  private:
    xs::node::GameNode node_;
    std::thread run_thread_{};
    std::atomic_bool run_completed_{false};
    xs::node::NodeErrorCode run_result_{xs::node::NodeErrorCode::None};
    std::string run_error_{};
};

std::uint16_t ParsePortFromTcpEndpoint(std::string_view endpoint)
{
    const std::size_t colon = endpoint.rfind(':');
    if (colon == std::string_view::npos || colon + 1 >= endpoint.size())
    {
        return 0U;
    }

    return static_cast<std::uint16_t>(std::stoul(std::string(endpoint.substr(colon + 1))));
}

std::uint64_t CurrentUnixTimeMilliseconds()
{
    const std::int64_t now_unix_ms = xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow());
    return now_unix_ms > 0 ? static_cast<std::uint64_t>(now_unix_ms) : 0U;
}

bool TryReceiveMultipartMessage(void* socket, std::vector<std::vector<std::byte>>* frames)
{
    if (socket == nullptr || frames == nullptr)
    {
        return false;
    }

    frames->clear();
    while (true)
    {
        zmq_msg_t frame;
        zmq_msg_init(&frame);
        const int receive_result = zmq_msg_recv(&frame, socket, frames->empty() ? ZMQ_DONTWAIT : 0);
        if (receive_result < 0)
        {
            const int error_code = zmq_errno();
            zmq_msg_close(&frame);
            if (frames->empty() && error_code == EAGAIN)
            {
                return false;
            }

            XS_CHECK_MSG(false, zmq_strerror(error_code));
            frames->clear();
            return false;
        }

        const auto* data = static_cast<const std::byte*>(zmq_msg_data(&frame));
        const std::size_t size = zmq_msg_size(&frame);
        frames->emplace_back(data, data + size);

        const bool has_more = zmq_msg_more(&frame) != 0;
        zmq_msg_close(&frame);
        if (!has_more)
        {
            return true;
        }
    }
}

bool SendRouterReply(
    void* router_socket,
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    const void* routing_id_data =
        routing_id.empty() ? static_cast<const void*>("") : static_cast<const void*>(routing_id.data());
    if (zmq_send(router_socket, routing_id_data, routing_id.size(), ZMQ_SNDMORE) < 0)
    {
        XS_CHECK_MSG(false, zmq_strerror(zmq_errno()));
        return false;
    }

    const void* payload_data = payload.empty() ? static_cast<const void*>("") : static_cast<const void*>(payload.data());
    if (zmq_send(router_socket, payload_data, payload.size(), 0) < 0)
    {
        XS_CHECK_MSG(false, zmq_strerror(zmq_errno()));
        return false;
    }

    return true;
}

std::vector<std::byte> EncodeRegisterSuccessPacket(
    std::uint32_t seq,
    std::uint32_t heartbeat_interval_ms,
    std::uint32_t heartbeat_timeout_ms)
{
    const xs::net::RegisterSuccessResponse response{
        .heartbeat_interval_ms = heartbeat_interval_ms,
        .heartbeat_timeout_ms = heartbeat_timeout_ms,
        .server_now_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::array<std::byte, xs::net::kRegisterSuccessResponseSize> body{};
    XS_CHECK(xs::net::EncodeRegisterSuccessResponse(response, body) == xs::net::RegisterCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerRegisterMsgId,
        seq,
        static_cast<std::uint16_t>(xs::net::PacketFlag::Response),
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

std::vector<std::byte> EncodeHeartbeatSuccessPacketWithFlags(
    std::uint32_t seq,
    std::uint16_t flags,
    std::uint32_t heartbeat_interval_ms,
    std::uint32_t heartbeat_timeout_ms)
{
    const xs::net::HeartbeatSuccessResponse response{
        .heartbeat_interval_ms = heartbeat_interval_ms,
        .heartbeat_timeout_ms = heartbeat_timeout_ms,
        .server_now_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::array<std::byte, xs::net::kHeartbeatSuccessResponseSize> body{};
    XS_CHECK(xs::net::EncodeHeartbeatSuccessResponse(response, body) == xs::net::HeartbeatCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerHeartbeatMsgId,
        seq,
        flags,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

std::vector<std::byte> EncodeHeartbeatSuccessPacket(
    std::uint32_t seq,
    std::uint32_t heartbeat_interval_ms,
    std::uint32_t heartbeat_timeout_ms)
{
    return EncodeHeartbeatSuccessPacketWithFlags(
        seq,
        static_cast<std::uint16_t>(xs::net::PacketFlag::Response),
        heartbeat_interval_ms,
        heartbeat_timeout_ms);
}

std::vector<std::byte> EncodeClusterNodesOnlineNotifyPacket(
    bool all_nodes_online,
    std::uint16_t flags = 0U,
    std::uint32_t seq = xs::net::kPacketSeqNone)
{
    const xs::net::ClusterNodesOnlineNotify notify{
        .all_nodes_online = all_nodes_online,
        .status_flags = 0U,
        .server_now_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::array<std::byte, xs::net::kClusterNodesOnlineNotifySize> body{};
    XS_CHECK(
        xs::net::EncodeClusterNodesOnlineNotify(notify, body) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerClusterNodesOnlineNotifyMsgId,
        seq,
        flags,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

std::vector<std::byte> EncodeServerStubOwnershipSyncPacket(
    const xs::net::ServerStubOwnershipSync& sync,
    std::uint16_t flags = 0U,
    std::uint32_t seq = xs::net::kPacketSeqNone)
{
    std::size_t wire_size = 0U;
    XS_CHECK(
        xs::net::GetServerStubOwnershipSyncWireSize(sync, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> body(wire_size);
    XS_CHECK(
        xs::net::EncodeServerStubOwnershipSync(sync, body) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerServerStubOwnershipSyncMsgId,
        seq,
        flags,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

std::vector<std::byte> EncodeRelayForwardStubCallPacket(std::string_view source_game_node_id,
                                                        std::string_view target_game_node_id,
                                                        std::string_view target_stub_type,
                                                        std::uint32_t stub_call_msg_id,
                                                        std::span<const std::byte> payload)
{
    const xs::net::RelayForwardStubCall relay_message{
        .source_game_node_id = std::string(source_game_node_id),
        .target_game_node_id = std::string(target_game_node_id),
        .target_stub_type = std::string(target_stub_type),
        .stub_call_msg_id = stub_call_msg_id,
        .relay_flags = 0u,
        .payload = std::vector<std::byte>(payload.begin(), payload.end()),
    };

    std::size_t wire_size = 0u;
    XS_CHECK(
        xs::net::GetRelayForwardStubCallWireSize(relay_message, &wire_size) == xs::net::RelayCodecErrorCode::None);

    std::vector<std::byte> body(wire_size);
    XS_CHECK(xs::net::EncodeRelayForwardStubCall(relay_message, body) == xs::net::RelayCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kRelayForwardStubCallMsgId,
        xs::net::kPacketSeqNone,
        0U,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

void TestGameNodeRegistersAndRefreshesHeartbeatAgainstRealGm()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-real-gm");
    const std::uint16_t gm_inner_port = AcquireLoopbackPort();
    const std::uint16_t gm_control_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, gm_inner_port, gm_control_port, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGmNode gm_node(config_path);
    if (!gm_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        gm_node.StopAndJoin();
        (void)gm_node.Uninit();
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gm_node]() {
        const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
        return std::any_of(snapshot.begin(), snapshot.end(), [](const xs::node::InnerNetworkSession& entry) {
            return entry.node_id == "Game0";
        });
    }));

    std::uint64_t initial_heartbeat_at_unix_ms = 0U;
    {
        const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
        const auto game_entry = std::find_if(snapshot.begin(), snapshot.end(), [](const xs::node::InnerNetworkSession& entry) {
            return entry.node_id == "Game0";
        });
        XS_CHECK(game_entry != snapshot.end());
        if (game_entry != snapshot.end())
        {
            XS_CHECK(game_entry->process_type == xs::core::ProcessType::Game);
            XS_CHECK(game_entry->inner_network_endpoint.port == 7100u);
            XS_CHECK(game_entry->last_heartbeat_at_unix_ms != 0U);
            initial_heartbeat_at_unix_ms = game_entry->last_heartbeat_at_unix_ms;
        }
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(7), [&gm_node, initial_heartbeat_at_unix_ms]() {
        const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
        const auto game_entry = std::find_if(snapshot.begin(), snapshot.end(), [](const xs::node::InnerNetworkSession& entry) {
            return entry.node_id == "Game0";
        });
        return game_entry != snapshot.end() &&
            game_entry->last_heartbeat_at_unix_ms > initial_heartbeat_at_unix_ms;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());
    XS_CHECK(gm_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node sent GM register request.") != std::string::npos);
    XS_CHECK(log_text.find("Game node accepted GM register success response.") != std::string::npos);
    XS_CHECK(log_text.find("GM accepted register request.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGameNodeAcceptsClusterNodesOnlineNotify()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-cluster-nodes-online");

    RawZmqSocket router(ZMQ_ROUTER);
    XS_CHECK(router.IsValid());

    const std::string gm_endpoint = router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, ParsePortFromTcpEndpoint(gm_endpoint), AcquireLoopbackPort(), &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));
    XS_CHECK(!game_node.node().all_nodes_online());
    XS_CHECK(game_node.node().cluster_nodes_online_server_now_unix_ms() == 0U);

    std::vector<std::vector<std::byte>> router_frames;
    bool register_accepted = false;
    bool cluster_nodes_online_sent = false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(router.socket(), &router_frames))
        {
            XS_CHECK(router_frames.size() == 2u);
            if (router_frames.size() != 2u)
            {
                break;
            }

            xs::net::PacketView packet{};
            XS_CHECK(xs::net::DecodePacket(router_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
            if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
            {
                xs::net::RegisterRequest request{};
                XS_CHECK(xs::net::DecodeRegisterRequest(packet.payload, &request) == xs::net::RegisterCodecErrorCode::None);
                XS_CHECK(request.node_id == "Game0");
                register_accepted = true;

                const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], response));

                const std::vector<std::byte> notify = EncodeClusterNodesOnlineNotifyPacket(true);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], notify));
                cluster_nodes_online_sent = true;
            }
        }

        if (cluster_nodes_online_sent && game_node.node().all_nodes_online())
        {
            break;
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(register_accepted);
    XS_CHECK(cluster_nodes_online_sent);
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().all_nodes_online() &&
            game_node.node().cluster_nodes_online_server_now_unix_ms() != 0U;
    }));

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node accepted GM cluster nodes online notify.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGameNodeRejectsClusterNodesOnlineNotifyBeforeRegisterCompletes()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-cluster-nodes-online-before-register");

    RawZmqSocket router(ZMQ_ROUTER);
    XS_CHECK(router.IsValid());

    const std::string gm_endpoint = router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, ParsePortFromTcpEndpoint(gm_endpoint), AcquireLoopbackPort(), &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::vector<std::vector<std::byte>> router_frames;
    bool register_response_sent = false;
    bool pre_register_notify_sent = false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(router.socket(), &router_frames))
        {
            XS_CHECK(router_frames.size() == 2u);
            if (router_frames.size() != 2u)
            {
                break;
            }

            xs::net::PacketView packet{};
            XS_CHECK(xs::net::DecodePacket(router_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
            if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
            {
                const std::vector<std::byte> notify = EncodeClusterNodesOnlineNotifyPacket(true);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], notify));
                pre_register_notify_sent = true;

                const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], response));
                register_response_sent = true;
                break;
            }
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(pre_register_notify_sent);
    XS_CHECK(register_response_sent);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    XS_CHECK(!game_node.node().all_nodes_online());
    XS_CHECK(game_node.node().cluster_nodes_online_server_now_unix_ms() == 0U);

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node ignored GM cluster nodes online notify before registration completed.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGameNodeRejectsClusterNodesOnlineNotifyWithInvalidEnvelope()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-cluster-nodes-online-invalid-envelope");

    RawZmqSocket router(ZMQ_ROUTER);
    XS_CHECK(router.IsValid());

    const std::string gm_endpoint = router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, ParsePortFromTcpEndpoint(gm_endpoint), AcquireLoopbackPort(), &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::vector<std::vector<std::byte>> router_frames;
    bool invalid_notify_sent = false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(router.socket(), &router_frames))
        {
            XS_CHECK(router_frames.size() == 2u);
            if (router_frames.size() != 2u)
            {
                break;
            }

            xs::net::PacketView packet{};
            XS_CHECK(xs::net::DecodePacket(router_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
            if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
            {
                const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], response));

                const std::vector<std::byte> notify = EncodeClusterNodesOnlineNotifyPacket(
                    true,
                    static_cast<std::uint16_t>(xs::net::PacketFlag::Response));
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], notify));
                invalid_notify_sent = true;
                break;
            }
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(invalid_notify_sent);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    XS_CHECK(!game_node.node().all_nodes_online());
    XS_CHECK(game_node.node().cluster_nodes_online_server_now_unix_ms() == 0U);

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node ignored GM cluster nodes online notify with an invalid envelope.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGameNodeStartsGateRegisterOnlyAfterClusterNodesOnlineNotify()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-gate-connect-after-all-nodes-online");

    RawZmqSocket gm_router(ZMQ_ROUTER);
    RawZmqSocket gate0_router(ZMQ_ROUTER);
    RawZmqSocket gate1_router(ZMQ_ROUTER);
    XS_CHECK(gm_router.IsValid());
    XS_CHECK(gate0_router.IsValid());
    XS_CHECK(gate1_router.IsValid());

    const std::string gm_endpoint = gm_router.BindLoopbackTcp();
    const std::string gate0_endpoint = gate0_router.BindLoopbackTcp();
    const std::string gate1_endpoint = gate1_router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());
    XS_CHECK(!gate0_endpoint.empty());
    XS_CHECK(!gate1_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            ParsePortFromTcpEndpoint(gm_endpoint),
            AcquireLoopbackPort(),
            &config_path,
            {
                ParsePortFromTcpEndpoint(gate0_endpoint),
                ParsePortFromTcpEndpoint(gate1_endpoint),
            }))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));
    XS_CHECK(game_node.node().inner_connection_state("Gate0") == xs::ipc::ZmqConnectionState::Stopped);
    XS_CHECK(game_node.node().inner_connection_state("Gate1") == xs::ipc::ZmqConnectionState::Stopped);

    std::vector<std::vector<std::byte>> gm_frames;
    std::vector<std::vector<std::byte>> gate0_frames;
    std::vector<std::vector<std::byte>> gate1_frames;
    bool gm_register_accepted = false;
    bool gate_message_before_notify = false;

    const auto register_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < register_deadline)
    {
        if (TryReceiveMultipartMessage(gate0_router.socket(), &gate0_frames) ||
            TryReceiveMultipartMessage(gate1_router.socket(), &gate1_frames))
        {
            gate_message_before_notify = true;
            break;
        }

        if (TryReceiveMultipartMessage(gm_router.socket(), &gm_frames))
        {
            XS_CHECK(gm_frames.size() == 2U);
            if (gm_frames.size() != 2U)
            {
                break;
            }

            xs::net::PacketView packet{};
            XS_CHECK(xs::net::DecodePacket(gm_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
            if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
            {
                xs::net::RegisterRequest request{};
                XS_CHECK(xs::net::DecodeRegisterRequest(packet.payload, &request) == xs::net::RegisterCodecErrorCode::None);
                XS_CHECK(request.node_id == "Game0");

                const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 5000U, 15000U);
                XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], response));
                gm_register_accepted = true;
                break;
            }
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(gm_register_accepted);
    XS_CHECK(!gate_message_before_notify);

    const auto quiet_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    while (std::chrono::steady_clock::now() < quiet_deadline)
    {
        if (TryReceiveMultipartMessage(gate0_router.socket(), &gate0_frames) ||
            TryReceiveMultipartMessage(gate1_router.socket(), &gate1_frames))
        {
            gate_message_before_notify = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(!gate_message_before_notify);
    XS_CHECK(game_node.node().inner_connection_state("Gate0") == xs::ipc::ZmqConnectionState::Stopped);
    XS_CHECK(game_node.node().inner_connection_state("Gate1") == xs::ipc::ZmqConnectionState::Stopped);

    const std::vector<std::byte> notify = EncodeClusterNodesOnlineNotifyPacket(true);
    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], notify));
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().all_nodes_online();
    }));

    bool gate0_register_accepted = false;
    bool gate1_register_accepted = false;
    bool gate0_heartbeat_accepted = false;
    bool gate1_heartbeat_accepted = false;

    const auto gate_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < gate_deadline)
    {
        if (TryReceiveMultipartMessage(gate0_router.socket(), &gate0_frames))
        {
            XS_CHECK(gate0_frames.size() == 2U);
            if (gate0_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate0_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate0_router.socket(), gate0_frames[0], response));
                    gate0_register_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate0_router.socket(), gate0_frames[0], response));
                    gate0_heartbeat_accepted = true;
                }
            }
        }

        if (TryReceiveMultipartMessage(gate1_router.socket(), &gate1_frames))
        {
            XS_CHECK(gate1_frames.size() == 2U);
            if (gate1_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate1_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate1_router.socket(), gate1_frames[0], response));
                    gate1_register_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate1_router.socket(), gate1_frames[0], response));
                    gate1_heartbeat_accepted = true;
                }
            }
        }

        if (gate0_register_accepted &&
            gate1_register_accepted &&
            gate0_heartbeat_accepted &&
            gate1_heartbeat_accepted)
        {
            break;
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(gate0_register_accepted);
    XS_CHECK(gate1_register_accepted);
    XS_CHECK(gate0_heartbeat_accepted);
    XS_CHECK(gate1_heartbeat_accepted);
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().inner_connection_state("Gate0") == xs::ipc::ZmqConnectionState::Connected &&
            game_node.node().inner_connection_state("Gate1") == xs::ipc::ZmqConnectionState::Connected;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node sent Gate register request.") != std::string::npos);
    XS_CHECK(log_text.find("Game node accepted Gate register success response.") != std::string::npos);
    CleanupTestDirectory(base_path);
}

void TestGameNodeNormalizesWildcardGateConnectorEndpoints()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-gate-wildcard-endpoint");

    RawZmqSocket gm_router(ZMQ_ROUTER);
    RawZmqSocket gate_router(ZMQ_ROUTER);
    XS_CHECK(gm_router.IsValid());
    XS_CHECK(gate_router.IsValid());

    const std::string gm_endpoint = gm_router.BindLoopbackTcp();
    const std::string gate_endpoint = gate_router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());
    XS_CHECK(!gate_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            ParsePortFromTcpEndpoint(gm_endpoint),
            AcquireLoopbackPort(),
            &config_path,
            {ParsePortFromTcpEndpoint(gate_endpoint)},
            7100U,
            "0.0.0.0"))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    std::vector<std::vector<std::byte>> gm_frames;
    std::vector<std::vector<std::byte>> gate_frames;
    bool gm_register_accepted = false;
    bool all_nodes_online_sent = false;
    bool gate_register_accepted = false;
    bool gate_heartbeat_accepted = false;
    bool mesh_ready_reported = false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(gm_router.socket(), &gm_frames))
        {
            XS_CHECK(gm_frames.size() == 2U);
            if (gm_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gm_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 5000U, 15000U);
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], response));
                    gm_register_accepted = true;

                    if (!all_nodes_online_sent)
                    {
                        const std::vector<std::byte> notify = EncodeClusterNodesOnlineNotifyPacket(true);
                        XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], notify));
                        all_nodes_online_sent = true;
                    }
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 5000U, 15000U);
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], response));
                }
                else if (packet.header.msg_id == xs::net::kInnerGameGateMeshReadyReportMsgId)
                {
                    xs::net::GameGateMeshReadyReport report{};
                    XS_CHECK(
                        xs::net::DecodeGameGateMeshReadyReport(packet.payload, &report) ==
                        xs::net::InnerClusterCodecErrorCode::None);
                    mesh_ready_reported = true;
                }
            }
        }

        if (TryReceiveMultipartMessage(gate_router.socket(), &gate_frames))
        {
            XS_CHECK(gate_frames.size() == 2U);
            if (gate_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate_router.socket(), gate_frames[0], response));
                    gate_register_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate_router.socket(), gate_frames[0], response));
                    gate_heartbeat_accepted = true;
                }
            }
        }

        if (mesh_ready_reported && gate_register_accepted && gate_heartbeat_accepted)
        {
            break;
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(gm_register_accepted);
    XS_CHECK(all_nodes_online_sent);
    XS_CHECK(gate_register_accepted);
    XS_CHECK(gate_heartbeat_accepted);
    XS_CHECK(mesh_ready_reported);
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().mesh_ready() &&
            game_node.node().inner_connection_state("Gate0") == xs::ipc::ZmqConnectionState::Connected;
    }));

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node sent Gate register request.") != std::string::npos);
    XS_CHECK(log_text.find("Game node sent mesh ready report.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGameNodeReportsMeshReadyAndAppliesOwnershipAfterGateMeshCompletes()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-mesh-ready-ownership");

    RawZmqSocket gm_router(ZMQ_ROUTER);
    RawZmqSocket gate0_router(ZMQ_ROUTER);
    RawZmqSocket gate1_router(ZMQ_ROUTER);
    XS_CHECK(gm_router.IsValid());
    XS_CHECK(gate0_router.IsValid());
    XS_CHECK(gate1_router.IsValid());

    const std::string gm_endpoint = gm_router.BindLoopbackTcp();
    const std::string gate0_endpoint = gate0_router.BindLoopbackTcp();
    const std::string gate1_endpoint = gate1_router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());
    XS_CHECK(!gate0_endpoint.empty());
    XS_CHECK(!gate1_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            ParsePortFromTcpEndpoint(gm_endpoint),
            AcquireLoopbackPort(),
            &config_path,
            {
                ParsePortFromTcpEndpoint(gate0_endpoint),
                ParsePortFromTcpEndpoint(gate1_endpoint),
            }))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    const xs::net::ServerStubOwnershipSync accepted_sync{
        .assignment_epoch = 5U,
        .status_flags = 0U,
        .assignments = {
            xs::net::ServerStubOwnershipEntry{
                .entity_type = "MatchStub",
                .entity_id = "unknown",
                .owner_game_node_id = "Game0",
                .entry_flags = 0U,
            },
            xs::net::ServerStubOwnershipEntry{
                .entity_type = "ChatStub",
                .entity_id = "unknown",
                .owner_game_node_id = "Game9",
                .entry_flags = 0U,
            },
            xs::net::ServerStubOwnershipEntry{
                .entity_type = "LeaderboardStub",
                .entity_id = "unknown",
                .owner_game_node_id = "Game0",
                .entry_flags = 0U,
            },
        },
        .server_now_unix_ms = CurrentUnixTimeMilliseconds(),
    };
    const xs::net::ServerStubOwnershipSync stale_sync{
        .assignment_epoch = accepted_sync.assignment_epoch - 1U,
        .status_flags = 0U,
        .assignments = {
            xs::net::ServerStubOwnershipEntry{
                .entity_type = "MatchStub",
                .entity_id = "unknown",
                .owner_game_node_id = "Game0",
                .entry_flags = 0U,
            },
            xs::net::ServerStubOwnershipEntry{
                .entity_type = "ChatStub",
                .entity_id = "unknown",
                .owner_game_node_id = "Game0",
                .entry_flags = 0U,
            },
        },
        .server_now_unix_ms = accepted_sync.server_now_unix_ms + 1U,
    };

    std::vector<std::vector<std::byte>> gm_frames;
    std::vector<std::vector<std::byte>> gate0_frames;
    std::vector<std::vector<std::byte>> gate1_frames;
    std::vector<std::byte> gm_routing_id;
    bool gm_register_accepted = false;
    bool all_nodes_online_sent = false;
    bool gm_heartbeat_accepted = false;
    bool gate0_register_accepted = false;
    bool gate1_register_accepted = false;
    bool gate0_heartbeat_accepted = false;
    bool gate1_heartbeat_accepted = false;
    bool mesh_ready_reported = false;
    bool mesh_ready_reported_before_gate_mesh = false;
    bool ownership_sync_sent = false;
    bool service_ready_reported = false;
    bool service_ready_reported_before_ownership = false;
    xs::net::GameServiceReadyReport service_ready_report{};

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(gm_router.socket(), &gm_frames))
        {
            XS_CHECK(gm_frames.size() == 2U);
            if (gm_frames.size() == 2U)
            {
                gm_routing_id = gm_frames[0];

                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gm_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 5000U, 15000U);
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], response));
                    gm_register_accepted = true;

                    if (!all_nodes_online_sent)
                    {
                        const std::vector<std::byte> notify = EncodeClusterNodesOnlineNotifyPacket(true);
                        XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], notify));
                        all_nodes_online_sent = true;
                    }
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 5000U, 15000U);
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], response));
                    gm_heartbeat_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerGameGateMeshReadyReportMsgId)
                {
                    XS_CHECK(packet.header.flags == 0U);
                    XS_CHECK(packet.header.seq == xs::net::kPacketSeqNone);

                    xs::net::GameGateMeshReadyReport report{};
                    XS_CHECK(
                        xs::net::DecodeGameGateMeshReadyReport(packet.payload, &report) ==
                        xs::net::InnerClusterCodecErrorCode::None);
                    XS_CHECK(report.status_flags == 0U);
                    XS_CHECK(report.reported_at_unix_ms != 0U);

                    if (!(gate0_register_accepted &&
                          gate1_register_accepted &&
                          gate0_heartbeat_accepted &&
                          gate1_heartbeat_accepted))
                    {
                        mesh_ready_reported_before_gate_mesh = true;
                    }

                    if (!ownership_sync_sent)
                    {
                        const std::vector<std::byte> ownership_packet = EncodeServerStubOwnershipSyncPacket(accepted_sync);
                        XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], ownership_packet));
                        ownership_sync_sent = true;
                    }

                    mesh_ready_reported = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerGameServiceReadyReportMsgId)
                {
                    XS_CHECK(packet.header.flags == 0U);
                    XS_CHECK(packet.header.seq == xs::net::kPacketSeqNone);

                    XS_CHECK(
                        xs::net::DecodeGameServiceReadyReport(packet.payload, &service_ready_report) ==
                        xs::net::InnerClusterCodecErrorCode::None);
                    XS_CHECK(service_ready_report.status_flags == 0U);
                    XS_CHECK(service_ready_report.reported_at_unix_ms != 0U);

                    if (!ownership_sync_sent)
                    {
                        service_ready_reported_before_ownership = true;
                    }

                    service_ready_reported = true;
                }
            }
        }

        if (TryReceiveMultipartMessage(gate0_router.socket(), &gate0_frames))
        {
            XS_CHECK(gate0_frames.size() == 2U);
            if (gate0_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate0_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate0_router.socket(), gate0_frames[0], response));
                    gate0_register_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate0_router.socket(), gate0_frames[0], response));
                    gate0_heartbeat_accepted = true;
                }
            }
        }

        if (TryReceiveMultipartMessage(gate1_router.socket(), &gate1_frames))
        {
            XS_CHECK(gate1_frames.size() == 2U);
            if (gate1_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate1_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate1_router.socket(), gate1_frames[0], response));
                    gate1_register_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate1_router.socket(), gate1_frames[0], response));
                    gate1_heartbeat_accepted = true;
                }
            }
        }

        if (ownership_sync_sent &&
            service_ready_reported &&
            game_node.node().assignment_epoch() == accepted_sync.assignment_epoch)
        {
            break;
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(gm_register_accepted);
    XS_CHECK(all_nodes_online_sent);

    XS_CHECK(gate0_register_accepted);
    XS_CHECK(gate1_register_accepted);
    XS_CHECK(gate0_heartbeat_accepted);
    XS_CHECK(gate1_heartbeat_accepted);
    XS_CHECK(mesh_ready_reported);
    XS_CHECK(!mesh_ready_reported_before_gate_mesh);
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node, &accepted_sync]() {
        return game_node.node().mesh_ready() &&
            game_node.node().mesh_ready_reported_at_unix_ms() != 0U &&
            game_node.node().assignment_epoch() == accepted_sync.assignment_epoch &&
            game_node.node().ownership_server_now_unix_ms() == accepted_sync.server_now_unix_ms &&
            game_node.node().ownership_assignments().size() == accepted_sync.assignments.size() &&
            game_node.node().owned_stub_assignments().size() == 2U;
    }));

    const std::vector<xs::net::ServerStubOwnershipEntry> assignments = game_node.node().ownership_assignments();
    XS_CHECK(assignments.size() == accepted_sync.assignments.size());
    if (assignments.size() == accepted_sync.assignments.size())
    {
        XS_CHECK(assignments[0].entity_type == "MatchStub");
        XS_CHECK(assignments[0].owner_game_node_id == "Game0");
        XS_CHECK(assignments[1].entity_type == "ChatStub");
        XS_CHECK(assignments[1].owner_game_node_id == "Game9");
        XS_CHECK(assignments[2].entity_type == "LeaderboardStub");
        XS_CHECK(assignments[2].owner_game_node_id == "Game0");
    }

    const std::vector<xs::net::ServerStubOwnershipEntry> owned_assignments = game_node.node().owned_stub_assignments();
    XS_CHECK(owned_assignments.size() == 2U);
    if (owned_assignments.size() == 2U)
    {
        XS_CHECK(owned_assignments[0].entity_type == "MatchStub");
        XS_CHECK(owned_assignments[1].entity_type == "LeaderboardStub");
    }

    XS_CHECK(service_ready_reported);
    XS_CHECK(!service_ready_reported_before_ownership);
    XS_CHECK(service_ready_report.assignment_epoch == accepted_sync.assignment_epoch);
    XS_CHECK(service_ready_report.local_ready);
    XS_CHECK(service_ready_report.status_flags == 0U);
    XS_CHECK(service_ready_report.entries.size() == 2U);
    if (service_ready_report.entries.size() == 2U)
    {
        XS_CHECK(service_ready_report.entries[0].entity_type == "MatchStub");
        XS_CHECK(IsCanonicalGuidText(service_ready_report.entries[0].entity_id));
        XS_CHECK(service_ready_report.entries[0].ready);
        XS_CHECK(service_ready_report.entries[0].entry_flags == 0U);
        XS_CHECK(service_ready_report.entries[1].entity_type == "LeaderboardStub");
        XS_CHECK(IsCanonicalGuidText(service_ready_report.entries[1].entity_id));
        XS_CHECK(service_ready_report.entries[0].entity_id != service_ready_report.entries[1].entity_id);
        XS_CHECK(service_ready_report.entries[1].ready);
        XS_CHECK(service_ready_report.entries[1].entry_flags == 0U);
    }

    XS_CHECK(!gm_routing_id.empty());
    if (!gm_routing_id.empty())
    {
        const std::vector<std::byte> stale_packet = EncodeServerStubOwnershipSyncPacket(stale_sync);
        XS_CHECK(SendRouterReply(gm_router.socket(), gm_routing_id, stale_packet));
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        XS_CHECK(game_node.node().assignment_epoch() == accepted_sync.assignment_epoch);
        XS_CHECK(game_node.node().ownership_assignments().size() == accepted_sync.assignments.size());
        XS_CHECK(game_node.node().owned_stub_assignments().size() == 2U);

        const std::vector<std::byte> notify = EncodeClusterNodesOnlineNotifyPacket(false);
        XS_CHECK(SendRouterReply(gm_router.socket(), gm_routing_id, notify));
        XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node, &accepted_sync]() {
            return game_node.node().all_nodes_online() &&
                game_node.node().mesh_ready() &&
                game_node.node().assignment_epoch() == accepted_sync.assignment_epoch &&
                game_node.node().ownership_server_now_unix_ms() == accepted_sync.server_now_unix_ms &&
                game_node.node().ownership_assignments().size() == accepted_sync.assignments.size() &&
                game_node.node().owned_stub_assignments().size() == 2U;
        }));
    }

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node sent mesh ready report.") != std::string::npos);
    XS_CHECK(log_text.find("Game node sent service ready report.") != std::string::npos);
    XS_CHECK(log_text.find("Game node accepted GM ownership sync.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGameNodeSkipsServiceReadyReportWhenNoStubIsOwned()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-no-owned-service-ready");

    RawZmqSocket gm_router(ZMQ_ROUTER);
    RawZmqSocket gate0_router(ZMQ_ROUTER);
    RawZmqSocket gate1_router(ZMQ_ROUTER);
    XS_CHECK(gm_router.IsValid());
    XS_CHECK(gate0_router.IsValid());
    XS_CHECK(gate1_router.IsValid());

    const std::string gm_endpoint = gm_router.BindLoopbackTcp();
    const std::string gate0_endpoint = gate0_router.BindLoopbackTcp();
    const std::string gate1_endpoint = gate1_router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());
    XS_CHECK(!gate0_endpoint.empty());
    XS_CHECK(!gate1_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            ParsePortFromTcpEndpoint(gm_endpoint),
            AcquireLoopbackPort(),
            &config_path,
            {
                ParsePortFromTcpEndpoint(gate0_endpoint),
                ParsePortFromTcpEndpoint(gate1_endpoint),
            }))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    const xs::net::ServerStubOwnershipSync no_owned_sync{
        .assignment_epoch = 6U,
        .status_flags = 0U,
        .assignments = {
            xs::net::ServerStubOwnershipEntry{
                .entity_type = "MatchStub",
                .entity_id = "unknown",
                .owner_game_node_id = "Game9",
                .entry_flags = 0U,
            },
            xs::net::ServerStubOwnershipEntry{
                .entity_type = "ChatStub",
                .entity_id = "unknown",
                .owner_game_node_id = "Game9",
                .entry_flags = 0U,
            },
            xs::net::ServerStubOwnershipEntry{
                .entity_type = "LeaderboardStub",
                .entity_id = "unknown",
                .owner_game_node_id = "Game9",
                .entry_flags = 0U,
            },
        },
        .server_now_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::vector<std::vector<std::byte>> gm_frames;
    std::vector<std::vector<std::byte>> gate0_frames;
    std::vector<std::vector<std::byte>> gate1_frames;
    bool gm_register_accepted = false;
    bool all_nodes_online_sent = false;
    bool gm_heartbeat_accepted = false;
    bool gate0_register_accepted = false;
    bool gate1_register_accepted = false;
    bool gate0_heartbeat_accepted = false;
    bool gate1_heartbeat_accepted = false;
    bool ownership_sync_sent = false;
    bool service_ready_reported = false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(gm_router.socket(), &gm_frames))
        {
            XS_CHECK(gm_frames.size() == 2U);
            if (gm_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gm_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 5000U, 15000U);
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], response));
                    gm_register_accepted = true;

                    if (!all_nodes_online_sent)
                    {
                        const std::vector<std::byte> notify = EncodeClusterNodesOnlineNotifyPacket(true);
                        XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], notify));
                        all_nodes_online_sent = true;
                    }
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 5000U, 15000U);
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], response));
                    gm_heartbeat_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerGameGateMeshReadyReportMsgId)
                {
                    xs::net::GameGateMeshReadyReport report{};
                    XS_CHECK(
                        xs::net::DecodeGameGateMeshReadyReport(packet.payload, &report) ==
                        xs::net::InnerClusterCodecErrorCode::None);

                    if (!ownership_sync_sent)
                    {
                        const std::vector<std::byte> ownership_packet = EncodeServerStubOwnershipSyncPacket(no_owned_sync);
                        XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], ownership_packet));
                        ownership_sync_sent = true;
                    }
                }
                else if (packet.header.msg_id == xs::net::kInnerGameServiceReadyReportMsgId)
                {
                    service_ready_reported = true;
                }
            }
        }

        if (TryReceiveMultipartMessage(gate0_router.socket(), &gate0_frames))
        {
            XS_CHECK(gate0_frames.size() == 2U);
            if (gate0_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate0_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate0_router.socket(), gate0_frames[0], response));
                    gate0_register_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate0_router.socket(), gate0_frames[0], response));
                    gate0_heartbeat_accepted = true;
                }
            }
        }

        if (TryReceiveMultipartMessage(gate1_router.socket(), &gate1_frames))
        {
            XS_CHECK(gate1_frames.size() == 2U);
            if (gate1_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate1_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    xs::net::RegisterRequest request{};
                    XS_CHECK(
                        xs::net::DecodeRegisterRequest(packet.payload, &request) ==
                        xs::net::RegisterCodecErrorCode::None);
                    XS_CHECK(request.node_id == "Game0");

                    const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate1_router.socket(), gate1_frames[0], response));
                    gate1_register_accepted = true;
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    xs::net::HeartbeatRequest request{};
                    XS_CHECK(
                        xs::net::DecodeHeartbeatRequest(packet.payload, &request) ==
                        xs::net::HeartbeatCodecErrorCode::None);

                    const std::vector<std::byte> response = EncodeHeartbeatSuccessPacket(packet.header.seq, 200U, 600U);
                    XS_CHECK(SendRouterReply(gate1_router.socket(), gate1_frames[0], response));
                    gate1_heartbeat_accepted = true;
                }
            }
        }

        if (ownership_sync_sent &&
            game_node.node().assignment_epoch() == no_owned_sync.assignment_epoch)
        {
            break;
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto no_report_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < no_report_deadline && !service_ready_reported)
    {
        if (TryReceiveMultipartMessage(gm_router.socket(), &gm_frames))
        {
            XS_CHECK(gm_frames.size() == 2U);
            if (gm_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gm_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
                if (packet.header.msg_id == xs::net::kInnerGameServiceReadyReportMsgId)
                {
                    service_ready_reported = true;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(gm_register_accepted);
    XS_CHECK(all_nodes_online_sent);
    XS_CHECK(gate0_register_accepted);
    XS_CHECK(gate1_register_accepted);
    XS_CHECK(gate0_heartbeat_accepted);
    XS_CHECK(gate1_heartbeat_accepted);
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node, &no_owned_sync]() {
        return game_node.node().mesh_ready() &&
            game_node.node().assignment_epoch() == no_owned_sync.assignment_epoch &&
            game_node.node().ownership_assignments().size() == no_owned_sync.assignments.size() &&
            game_node.node().owned_stub_assignments().empty();
    }));
    XS_CHECK(!service_ready_reported);

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node accepted GM ownership sync.") != std::string::npos);
    XS_CHECK(log_text.find("Game node sent service ready report.") == std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGameNodeForwardsRemoteStubCallThroughGate()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-forward-stub-call-through-gate");

    RawZmqSocket gm_router(ZMQ_ROUTER);
    RawZmqSocket gate_router(ZMQ_ROUTER);
    XS_CHECK(gm_router.IsValid());
    XS_CHECK(gate_router.IsValid());

    const std::string gm_endpoint = gm_router.BindLoopbackTcp();
    const std::string gate_endpoint = gate_router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());
    XS_CHECK(!gate_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            ParsePortFromTcpEndpoint(gm_endpoint),
            AcquireLoopbackPort(),
            &config_path,
            {ParsePortFromTcpEndpoint(gate_endpoint)},
            AcquireLoopbackPort()))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::vector<std::byte> gm_routing_id;
    bool gm_register_accepted = false;
    bool all_nodes_online_sent = false;
    bool gate_register_accepted = false;
    bool gate_heartbeat_accepted = false;
    bool ownership_sync_sent = false;
    bool forwarded_stub_call_received = false;
    const xs::net::ServerStubOwnershipSync accepted_sync{
        .assignment_epoch = 31u,
        .status_flags = 0u,
        .assignments =
            {
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "OnlineStub",
                    .entity_id = "unknown",
                    .owner_game_node_id = "Game0",
                    .entry_flags = 0u,
                },
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "MatchStub",
                    .entity_id = "unknown",
                    .owner_game_node_id = "Game1",
                    .entry_flags = 0u,
                },
            },
        .server_now_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::vector<std::vector<std::byte>> gm_frames;
    std::vector<std::vector<std::byte>> gate_frames;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline && !forwarded_stub_call_received)
    {
        if (TryReceiveMultipartMessage(gm_router.socket(), &gm_frames))
        {
            XS_CHECK(gm_frames.size() == 2U);
            if (gm_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gm_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);

                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    gm_routing_id = gm_frames[0];
                    gm_register_accepted = true;
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U)));
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], EncodeHeartbeatSuccessPacket(packet.header.seq, 500U, 1500U)));
                }
                else if (packet.header.msg_id == xs::net::kInnerGameGateMeshReadyReportMsgId && !ownership_sync_sent)
                {
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], EncodeServerStubOwnershipSyncPacket(accepted_sync)));
                    ownership_sync_sent = true;
                }
            }
        }

        if (gm_register_accepted && !all_nodes_online_sent && !gm_routing_id.empty())
        {
            XS_CHECK(SendRouterReply(gm_router.socket(), gm_routing_id, EncodeClusterNodesOnlineNotifyPacket(true)));
            all_nodes_online_sent = true;
        }

        if (TryReceiveMultipartMessage(gate_router.socket(), &gate_frames))
        {
            XS_CHECK(gate_frames.size() == 2U);
            if (gate_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);

                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    gate_register_accepted = true;
                    XS_CHECK(SendRouterReply(gate_router.socket(), gate_frames[0], EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U)));
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    gate_heartbeat_accepted = true;
                    XS_CHECK(SendRouterReply(gate_router.socket(), gate_frames[0], EncodeHeartbeatSuccessPacket(packet.header.seq, 500U, 1500U)));
                }
                else if (packet.header.msg_id == xs::net::kRelayForwardStubCallMsgId)
                {
                    xs::net::RelayForwardStubCall relay_message{};
                    XS_CHECK(
                        xs::net::DecodeRelayForwardStubCall(packet.payload, &relay_message) ==
                        xs::net::RelayCodecErrorCode::None);
                    XS_CHECK(relay_message.source_game_node_id == "Game0");
                    XS_CHECK(relay_message.target_game_node_id == "Game1");
                    XS_CHECK(relay_message.target_stub_type == "MatchStub");
                    XS_CHECK(relay_message.stub_call_msg_id == 5101U);
                    XS_CHECK(std::string(reinterpret_cast<const char*>(relay_message.payload.data()), relay_message.payload.size()) ==
                             "online-startup-call");
                    forwarded_stub_call_received = true;
                    break;
                }
            }
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(gm_register_accepted);
    XS_CHECK(all_nodes_online_sent);
    XS_CHECK(gate_register_accepted);
    XS_CHECK(gate_heartbeat_accepted);
    XS_CHECK(ownership_sync_sent);
    XS_CHECK(forwarded_stub_call_received);

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    CleanupTestDirectory(base_path);
}

void TestGameNodeAcceptsForwardedStubCallFromGate()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-accept-forwarded-stub-call");

    RawZmqSocket gm_router(ZMQ_ROUTER);
    RawZmqSocket gate_router(ZMQ_ROUTER);
    XS_CHECK(gm_router.IsValid());
    XS_CHECK(gate_router.IsValid());

    const std::string gm_endpoint = gm_router.BindLoopbackTcp();
    const std::string gate_endpoint = gate_router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());
    XS_CHECK(!gate_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            ParsePortFromTcpEndpoint(gm_endpoint),
            AcquireLoopbackPort(),
            &config_path,
            {ParsePortFromTcpEndpoint(gate_endpoint)},
            AcquireLoopbackPort()))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::vector<std::byte> gm_routing_id;
    std::vector<std::byte> gate_routing_id;
    bool gm_register_accepted = false;
    bool all_nodes_online_sent = false;
    bool gate_register_accepted = false;
    bool gate_heartbeat_accepted = false;
    bool ownership_sync_sent = false;
    bool relay_sent = false;
    std::chrono::steady_clock::time_point relay_sent_at{};
    const xs::net::ServerStubOwnershipSync accepted_sync{
        .assignment_epoch = 37u,
        .status_flags = 0u,
        .assignments =
            {
                xs::net::ServerStubOwnershipEntry{
                    .entity_type = "MatchStub",
                    .entity_id = "unknown",
                    .owner_game_node_id = "Game0",
                    .entry_flags = 0u,
                },
            },
        .server_now_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::vector<std::vector<std::byte>> gm_frames;
    std::vector<std::vector<std::byte>> gate_frames;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(gm_router.socket(), &gm_frames))
        {
            XS_CHECK(gm_frames.size() == 2U);
            if (gm_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gm_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);

                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    gm_routing_id = gm_frames[0];
                    gm_register_accepted = true;
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U)));
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], EncodeHeartbeatSuccessPacket(packet.header.seq, 500U, 1500U)));
                }
                else if (packet.header.msg_id == xs::net::kInnerGameGateMeshReadyReportMsgId && !ownership_sync_sent)
                {
                    XS_CHECK(SendRouterReply(gm_router.socket(), gm_frames[0], EncodeServerStubOwnershipSyncPacket(accepted_sync)));
                    ownership_sync_sent = true;
                }
            }
        }

        if (gm_register_accepted && !all_nodes_online_sent && !gm_routing_id.empty())
        {
            XS_CHECK(SendRouterReply(gm_router.socket(), gm_routing_id, EncodeClusterNodesOnlineNotifyPacket(true)));
            all_nodes_online_sent = true;
        }

        if (TryReceiveMultipartMessage(gate_router.socket(), &gate_frames))
        {
            XS_CHECK(gate_frames.size() == 2U);
            if (gate_frames.size() == 2U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(gate_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);

                if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
                {
                    gate_routing_id = gate_frames[0];
                    gate_register_accepted = true;
                    XS_CHECK(SendRouterReply(gate_router.socket(), gate_frames[0], EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U)));
                }
                else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
                {
                    gate_heartbeat_accepted = true;
                    XS_CHECK(SendRouterReply(gate_router.socket(), gate_frames[0], EncodeHeartbeatSuccessPacket(packet.header.seq, 500U, 1500U)));
                }
            }
        }

        if (ownership_sync_sent &&
            gate_heartbeat_accepted &&
            game_node.node().assignment_epoch() == accepted_sync.assignment_epoch &&
            !relay_sent &&
            !gate_routing_id.empty())
        {
            const std::string stub_payload_text = "forwarded-from-gate";
            const std::span<const std::byte> stub_payload(
                reinterpret_cast<const std::byte*>(stub_payload_text.data()),
                stub_payload_text.size());
            XS_CHECK(
                SendRouterReply(
                    gate_router.socket(),
                    gate_routing_id,
                    EncodeRelayForwardStubCallPacket("Game9", "Game0", "MatchStub", 5101U, stub_payload)));
            relay_sent = true;
            relay_sent_at = std::chrono::steady_clock::now();
        }

        if (relay_sent && std::chrono::steady_clock::now() - relay_sent_at >= std::chrono::milliseconds(800))
        {
            break;
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(gm_register_accepted);
    XS_CHECK(all_nodes_online_sent);
    XS_CHECK(gate_register_accepted);
    XS_CHECK(gate_heartbeat_accepted);
    XS_CHECK(ownership_sync_sent);
    XS_CHECK(relay_sent);

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));
    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("MatchStub received call msgId=5101.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGameNodeRejectsHeartbeatResponseWithErrorFlag()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-gm-session-heartbeat-invalid-envelope");

    RawZmqSocket router(ZMQ_ROUTER);
    XS_CHECK(router.IsValid());

    const std::string gm_endpoint = router.BindLoopbackTcp();
    XS_CHECK(!gm_endpoint.empty());

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, ParsePortFromTcpEndpoint(gm_endpoint), AcquireLoopbackPort(), &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::vector<std::vector<std::byte>> router_frames;
    bool register_accepted = false;
    bool invalid_heartbeat_sent = false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(router.socket(), &router_frames))
        {
            XS_CHECK(router_frames.size() == 2u);
            if (router_frames.size() != 2u)
            {
                break;
            }

            xs::net::PacketView packet{};
            XS_CHECK(xs::net::DecodePacket(router_frames[1], &packet) == xs::net::PacketCodecErrorCode::None);
            if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
            {
                xs::net::RegisterRequest request{};
                XS_CHECK(xs::net::DecodeRegisterRequest(packet.payload, &request) == xs::net::RegisterCodecErrorCode::None);
                XS_CHECK(request.node_id == "Game0");
                register_accepted = true;

                const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], response));
            }
            else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
            {
                xs::net::HeartbeatRequest request{};
                XS_CHECK(
                    xs::net::DecodeHeartbeatRequest(packet.payload, &request) == xs::net::HeartbeatCodecErrorCode::None);

                const std::vector<std::byte> response = EncodeHeartbeatSuccessPacketWithFlags(
                    packet.header.seq,
                    static_cast<std::uint16_t>(xs::net::PacketFlag::Response) |
                        static_cast<std::uint16_t>(xs::net::PacketFlag::Error),
                    500U,
                    1500U);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], response));
                invalid_heartbeat_sent = true;
                break;
            }
        }

        if (game_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (invalid_heartbeat_sent)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());

    XS_CHECK(register_accepted);
    XS_CHECK(invalid_heartbeat_sent);

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node accepted GM register success response.") != std::string::npos);
    XS_CHECK(log_text.find("Game node ignored GM heartbeat response with an invalid envelope.") != std::string::npos);
    XS_CHECK(log_text.find("Game node accepted GM heartbeat success response.") == std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{

    TestGameNodeAcceptsClusterNodesOnlineNotify();
    TestGameNodeRejectsClusterNodesOnlineNotifyBeforeRegisterCompletes();
    TestGameNodeRejectsClusterNodesOnlineNotifyWithInvalidEnvelope();
    TestGameNodeStartsGateRegisterOnlyAfterClusterNodesOnlineNotify();
    TestGameNodeNormalizesWildcardGateConnectorEndpoints();
    TestGameNodeReportsMeshReadyAndAppliesOwnershipAfterGateMeshCompletes();
    TestGameNodeSkipsServiceReadyReportWhenNoStubIsOwned();
    TestGameNodeForwardsRemoteStubCallThroughGate();
    TestGameNodeAcceptsForwardedStubCallFromGate();
    TestGameNodeRejectsHeartbeatResponseWithErrorFlag();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " Game GM session test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include "GateNode.h"
#include "GmNode.h"
#include "Json.h"
#include "KcpPeer.h"
#include "TestManagedConfigJson.h"
#include "TimeUtils.h"
#include "message/HeartbeatCodec.h"
#include "message/InnerClusterCodec.h"
#include "message/PacketCodec.h"
#include "message/RelayCodec.h"
#include "message/RegisterCodec.h"

#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/ip/udp.hpp>

#include <zmq.h>

#include <array>
#include <atomic>
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

    [[nodiscard]] bool SetRoutingId(std::string_view routing_id)
    {
        return socket_ != nullptr && zmq_setsockopt(socket_, ZMQ_ROUTING_ID, routing_id.data(), routing_id.size()) == 0;
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

    [[nodiscard]] bool Connect(std::string_view endpoint)
    {
        return socket_ != nullptr && zmq_connect(socket_, endpoint.data()) == 0;
    }

    [[nodiscard]] bool Send(std::span<const std::byte> payload)
    {
        if (socket_ == nullptr)
        {
            return false;
        }

        const void* payload_data = payload.empty() ? static_cast<const void*>("") : static_cast<const void*>(payload.data());
        return zmq_send(socket_, payload_data, payload.size(), 0) >= 0;
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

std::vector<std::byte> BytesFromText(std::string_view value)
{
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());

    for (const char character : value)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }

    return bytes;
}

void SendDatagrams(
    asio::ip::udp::socket& socket,
    const asio::ip::udp::endpoint& target,
    const std::vector<std::vector<std::byte>>& datagrams)
{
    for (const auto& datagram : datagrams)
    {
        const std::size_t sent = socket.send_to(asio::buffer(datagram), target);
        XS_CHECK(sent == datagram.size());
    }
}

xs::core::Json MakeClusterConfigJson(
    const std::filesystem::path& base_path,
    std::uint16_t gm_inner_port,
    std::uint16_t gm_control_port,
    std::uint16_t gate_inner_port = 7000U,
    std::uint16_t game_inner_port = 7100U,
    std::uint16_t second_game_inner_port = 0U)
{
    const std::string root_log_dir = (base_path / "logs").string();
    xs::core::Json game_block = xs::core::Json::object();
    game_block["Game0"] = xs::core::Json{
        {"innerNetwork",
         xs::core::Json{
             {"listenEndpoint",
              xs::core::Json{{"host", "127.0.0.1"}, {"port", game_inner_port}}},
         }},
    };
    if (second_game_inner_port != 0U)
    {
        game_block["Game1"] = xs::core::Json{
            {"innerNetwork",
             xs::core::Json{
                 {"listenEndpoint",
                  xs::core::Json{{"host", "127.0.0.1"}, {"port", second_game_inner_port}}},
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
        {"gate",
         xs::core::Json{
             {"Gate0",
              xs::core::Json{
                  {"innerNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "127.0.0.1"}, {"port", gate_inner_port}}},
                   }},
                  {"clientNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "0.0.0.0"}, {"port", 4000}}},
                   }},
              }},
         }},
        {"game", game_block},
    };
}

bool WriteRuntimeConfig(
    const std::filesystem::path& base_path,
    std::uint16_t gm_inner_port,
    std::uint16_t gm_control_port,
    std::filesystem::path* file_path,
    std::uint16_t gate_inner_port = 7000U,
    std::uint16_t game_inner_port = 7100U,
    std::uint16_t second_game_inner_port = 0U)
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
            gate_inner_port,
            game_inner_port,
            second_game_inner_port));
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

class RunningGateNode final
{
  public:
    explicit RunningGateNode(const std::filesystem::path& config_path)
        : node_({
              .config_path = config_path,
              .node_id = "Gate0",
          })
    {
    }

    ~RunningGateNode()
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

    [[nodiscard]] xs::node::GateNode& node() noexcept
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
    xs::node::GateNode node_;
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

std::vector<std::byte> EncodeRegisterRequestPacket(
    std::uint32_t seq,
    std::string_view node_id,
    std::uint16_t process_type = static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
    std::uint16_t inner_port = 7100U)
{
    const xs::net::RegisterRequest request{
        .process_type = process_type,
        .process_flags = 0U,
        .node_id = std::string(node_id),
        .pid = 4321U,
        .started_at_unix_ms = CurrentUnixTimeMilliseconds(),
        .inner_network_endpoint = xs::net::Endpoint{
            .host = "127.0.0.1",
            .port = inner_port,
        },
        .build_version = "test-build",
        .capability_tags = {},
        .load = xs::net::LoadSnapshot{},
    };

    std::size_t payload_size = 0U;
    XS_CHECK(xs::net::GetRegisterRequestWireSize(request, &payload_size) == xs::net::RegisterCodecErrorCode::None);

    std::vector<std::byte> body(payload_size);
    XS_CHECK(xs::net::EncodeRegisterRequest(request, body) == xs::net::RegisterCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerRegisterMsgId,
        seq,
        0U,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

std::vector<std::byte> EncodeHeartbeatRequestPacket(std::uint32_t seq)
{
    const xs::net::HeartbeatRequest request{
        .sent_at_unix_ms = CurrentUnixTimeMilliseconds(),
        .status_flags = 0U,
        .load = xs::net::LoadSnapshot{},
    };

    std::array<std::byte, xs::net::kHeartbeatRequestSize> body{};
    XS_CHECK(xs::net::EncodeHeartbeatRequest(request, body) == xs::net::HeartbeatCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerHeartbeatMsgId,
        seq,
        0U,
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

std::vector<std::byte> EncodeClusterReadyNotifyPacket(
    std::uint64_t ready_epoch,
    bool cluster_ready)
{
    const xs::net::ClusterReadyNotify notify{
        .ready_epoch = ready_epoch,
        .cluster_ready = cluster_ready,
        .status_flags = 0U,
        .server_now_unix_ms = CurrentUnixTimeMilliseconds(),
    };

    std::array<std::byte, xs::net::kClusterReadyNotifySize> body{};
    XS_CHECK(
        xs::net::EncodeClusterReadyNotify(notify, body) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerClusterReadyNotifyMsgId,
        xs::net::kPacketSeqNone,
        0U,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

void TestGateNodeRegistersAndRefreshesHeartbeatAgainstRealGm()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gate-gm-session-real-gm");
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

    RunningGateNode gate_node(config_path);
    if (!gate_node.Start())
    {
        gm_node.StopAndJoin();
        (void)gm_node.Uninit();
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gate_node]() {
        return gate_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gm_node]() {
        const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
        return snapshot.size() == 1u && snapshot.front().node_id == "Gate0";
    }));

    std::uint64_t initial_heartbeat_at_unix_ms = 0U;
    {
        const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
        XS_CHECK(snapshot.size() == 1u);
        if (snapshot.size() == 1u)
        {
            XS_CHECK(snapshot.front().process_type == xs::core::ProcessType::Gate);
            XS_CHECK(snapshot.front().inner_network_endpoint.port == 7000u);
            XS_CHECK(snapshot.front().last_heartbeat_at_unix_ms != 0U);
            initial_heartbeat_at_unix_ms = snapshot.front().last_heartbeat_at_unix_ms;
        }
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(7), [&gm_node, initial_heartbeat_at_unix_ms]() {
        const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
        return snapshot.size() == 1u && snapshot.front().last_heartbeat_at_unix_ms > initial_heartbeat_at_unix_ms;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    gate_node.StopAndJoin();
    XS_CHECK_MSG(gate_node.run_result() == xs::node::NodeErrorCode::None, gate_node.run_error().data());
    XS_CHECK(gate_node.Uninit());

    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());
    XS_CHECK(gm_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Gate node sent GM register request.") != std::string::npos);
    XS_CHECK(log_text.find("Gate node accepted GM register success response.") != std::string::npos);
    XS_CHECK(log_text.find("GM accepted register request.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGateNodeOpensClientNetworkOnlyAfterClusterReadyNotify()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gate-gm-session-cluster-ready");

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

    RunningGateNode gate_node(config_path);
    if (!gate_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gate_node]() {
        return gate_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));
    XS_CHECK(!gate_node.node().cluster_ready());
    XS_CHECK(gate_node.node().cluster_ready_epoch() == 0U);
    XS_CHECK(!gate_node.node().client_network_running());
    XS_CHECK(gate_node.node().client_network_session_count() == 0U);

    std::vector<std::vector<std::byte>> router_frames;
    bool register_accepted = false;
    bool heartbeat_accepted = false;
    bool cluster_ready_sent = false;

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
                XS_CHECK(request.node_id == "Gate0");
                register_accepted = true;

                const std::vector<std::byte> response = EncodeRegisterSuccessPacket(packet.header.seq, 500U, 1500U);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], response));
            }
            else if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
            {
                xs::net::HeartbeatRequest request{};
                XS_CHECK(
                    xs::net::DecodeHeartbeatRequest(packet.payload, &request) == xs::net::HeartbeatCodecErrorCode::None);
                XS_CHECK(!gate_node.node().cluster_ready());
                XS_CHECK(!gate_node.node().client_network_running());

                heartbeat_accepted = true;
                const std::vector<std::byte> response =
                    EncodeHeartbeatSuccessPacket(packet.header.seq, 500U, 1500U);
                XS_CHECK(SendRouterReply(router.socket(), router_frames[0], response));

                if (!cluster_ready_sent)
                {
                    const std::vector<std::byte> notify = EncodeClusterReadyNotifyPacket(7U, true);
                    XS_CHECK(SendRouterReply(router.socket(), router_frames[0], notify));
                    cluster_ready_sent = true;
                }
            }
        }

        if (cluster_ready_sent && gate_node.node().client_network_running())
        {
            break;
        }

        if (gate_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(register_accepted);
    XS_CHECK(heartbeat_accepted);
    XS_CHECK(cluster_ready_sent);
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gate_node]() {
        return gate_node.node().cluster_ready() &&
            gate_node.node().cluster_ready_epoch() == 7U &&
            gate_node.node().client_network_running();
    }));
    XS_CHECK(gate_node.node().client_network_session_count() == 0U);

    asio::io_context client_io_context;
    asio::ip::udp::socket client_socket(
        client_io_context,
        asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0U));
    const asio::ip::udp::endpoint gate_client_endpoint(asio::ip::address_v4::loopback(), 4000U);

    xs::core::KcpConfig client_kcp_config;
    client_kcp_config.no_congestion_window = true;

    xs::net::KcpPeer client_peer({
        .conversation = 21U,
        .config = client_kcp_config,
    });
    XS_CHECK_MSG(client_peer.valid(), client_peer.last_error_message().data());
    XS_CHECK(client_peer.Send(BytesFromText("gate-client-first-payload")) == xs::net::KcpPeerErrorCode::None);
    XS_CHECK(client_peer.Flush(0U) == xs::net::KcpPeerErrorCode::None);
    SendDatagrams(client_socket, gate_client_endpoint, client_peer.ConsumeOutgoingDatagrams());

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gate_node]() {
        return gate_node.node().client_network_session_count() == 1U;
    }));

    gate_node.StopAndJoin();
    XS_CHECK_MSG(gate_node.run_result() == xs::node::NodeErrorCode::None, gate_node.run_error().data());
    XS_CHECK(gate_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Gate node accepted GM cluster ready notify.") != std::string::npos);
    XS_CHECK(log_text.find("Client network started.") != std::string::npos);
    XS_CHECK(log_text.find("Client session created.") != std::string::npos);
    XS_CHECK(log_text.find("Client network received a client payload.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGateNodeAcceptsGameRegisterAndHeartbeat()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gate-game-session-register-heartbeat");
    const std::uint16_t gate_inner_port = AcquireLoopbackPort();
    const std::string gate_endpoint = "tcp://127.0.0.1:" + std::to_string(gate_inner_port);

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            AcquireLoopbackPort(),
            AcquireLoopbackPort(),
            &config_path,
            gate_inner_port))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGateNode gate_node(config_path);
    if (!gate_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gate_node]() {
        return gate_node.node().game_inner_listener_state() == xs::ipc::ZmqListenerState::Listening;
    }));

    RawZmqSocket dealer(ZMQ_DEALER);
    XS_CHECK(dealer.IsValid());
    XS_CHECK(dealer.SetRoutingId("Game0"));
    XS_CHECK(dealer.Connect(gate_endpoint));

    std::vector<std::vector<std::byte>> dealer_frames;
    const std::vector<std::byte> register_packet = EncodeRegisterRequestPacket(1U, "Game0");
    XS_CHECK(dealer.Send(register_packet));

    bool register_response_received = false;
    const auto register_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < register_deadline)
    {
        if (TryReceiveMultipartMessage(dealer.socket(), &dealer_frames))
        {
            XS_CHECK(dealer_frames.size() == 1U);
            if (dealer_frames.size() == 1U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(dealer_frames[0], &packet) == xs::net::PacketCodecErrorCode::None);
                XS_CHECK(packet.header.msg_id == xs::net::kInnerRegisterMsgId);
                XS_CHECK(packet.header.flags == static_cast<std::uint16_t>(xs::net::PacketFlag::Response));
                XS_CHECK(packet.header.seq == 1U);

                xs::net::RegisterSuccessResponse response{};
                XS_CHECK(
                    xs::net::DecodeRegisterSuccessResponse(packet.payload, &response) ==
                    xs::net::RegisterCodecErrorCode::None);
                XS_CHECK(response.heartbeat_interval_ms != 0U);
                XS_CHECK(response.heartbeat_timeout_ms != 0U);
                register_response_received = true;
                break;
            }
        }

        if (gate_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(register_response_received);
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gate_node]() {
        return gate_node.node().inner_connection_state("Game0") == xs::ipc::ZmqConnectionState::Connected;
    }));

    const std::vector<std::byte> heartbeat_packet = EncodeHeartbeatRequestPacket(2U);
    XS_CHECK(dealer.Send(heartbeat_packet));

    bool heartbeat_response_received = false;
    const auto heartbeat_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < heartbeat_deadline)
    {
        if (TryReceiveMultipartMessage(dealer.socket(), &dealer_frames))
        {
            XS_CHECK(dealer_frames.size() == 1U);
            if (dealer_frames.size() == 1U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(dealer_frames[0], &packet) == xs::net::PacketCodecErrorCode::None);
                XS_CHECK(packet.header.msg_id == xs::net::kInnerHeartbeatMsgId);
                XS_CHECK(packet.header.flags == static_cast<std::uint16_t>(xs::net::PacketFlag::Response));
                XS_CHECK(packet.header.seq == 2U);

                xs::net::HeartbeatSuccessResponse response{};
                XS_CHECK(
                    xs::net::DecodeHeartbeatSuccessResponse(packet.payload, &response) ==
                    xs::net::HeartbeatCodecErrorCode::None);
                XS_CHECK(response.heartbeat_interval_ms != 0U);
                XS_CHECK(response.heartbeat_timeout_ms != 0U);
                heartbeat_response_received = true;
                break;
            }
        }

        if (gate_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(heartbeat_response_received);

    gate_node.StopAndJoin();
    XS_CHECK_MSG(gate_node.run_result() == xs::node::NodeErrorCode::None, gate_node.run_error().data());
    XS_CHECK(gate_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Gate node accepted Game register request.") != std::string::npos);
    CleanupTestDirectory(base_path);
}

void TestGateNodeRejectsUnknownGameRegister()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gate-game-session-unknown-register");
    const std::uint16_t gate_inner_port = AcquireLoopbackPort();
    const std::string gate_endpoint = "tcp://127.0.0.1:" + std::to_string(gate_inner_port);

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            AcquireLoopbackPort(),
            AcquireLoopbackPort(),
            &config_path,
            gate_inner_port))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGateNode gate_node(config_path);
    if (!gate_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gate_node]() {
        return gate_node.node().game_inner_listener_state() == xs::ipc::ZmqListenerState::Listening;
    }));

    RawZmqSocket dealer(ZMQ_DEALER);
    XS_CHECK(dealer.IsValid());
    XS_CHECK(dealer.SetRoutingId("Ghost0"));
    XS_CHECK(dealer.Connect(gate_endpoint));

    std::vector<std::vector<std::byte>> dealer_frames;
    const std::vector<std::byte> register_packet = EncodeRegisterRequestPacket(7U, "Ghost0");
    XS_CHECK(dealer.Send(register_packet));

    bool error_response_received = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(dealer.socket(), &dealer_frames))
        {
            XS_CHECK(dealer_frames.size() == 1U);
            if (dealer_frames.size() == 1U)
            {
                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(dealer_frames[0], &packet) == xs::net::PacketCodecErrorCode::None);
                XS_CHECK(packet.header.msg_id == xs::net::kInnerRegisterMsgId);
                XS_CHECK(
                    packet.header.flags ==
                    (static_cast<std::uint16_t>(xs::net::PacketFlag::Response) |
                     static_cast<std::uint16_t>(xs::net::PacketFlag::Error)));
                XS_CHECK(packet.header.seq == 7U);

                xs::net::RegisterErrorResponse response{};
                XS_CHECK(
                    xs::net::DecodeRegisterErrorResponse(packet.payload, &response) ==
                    xs::net::RegisterCodecErrorCode::None);
                XS_CHECK(response.error_code == 3003);
                error_response_received = true;
                break;
            }
        }

        if (gate_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(error_response_received);

    gate_node.StopAndJoin();
    XS_CHECK_MSG(gate_node.run_result() == xs::node::NodeErrorCode::None, gate_node.run_error().data());
    XS_CHECK(gate_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Gate node rejected Game register request.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestGateNodeForwardsStubCallBetweenGames()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gate-forward-stub-call-between-games");
    const std::uint16_t gate_inner_port = AcquireLoopbackPort();
    const std::string gate_endpoint = "tcp://127.0.0.1:" + std::to_string(gate_inner_port);
    const std::uint16_t game0_inner_port = AcquireLoopbackPort();
    const std::uint16_t game1_inner_port = AcquireLoopbackPort();

    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(
            base_path,
            AcquireLoopbackPort(),
            AcquireLoopbackPort(),
            &config_path,
            gate_inner_port,
            game0_inner_port,
            game1_inner_port))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGateNode gate_node(config_path);
    if (!gate_node.Start())
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&gate_node]() {
        return gate_node.node().game_inner_listener_state() == xs::ipc::ZmqListenerState::Listening;
    }));

    RawZmqSocket source_dealer(ZMQ_DEALER);
    RawZmqSocket target_dealer(ZMQ_DEALER);
    XS_CHECK(source_dealer.IsValid());
    XS_CHECK(target_dealer.IsValid());
    XS_CHECK(source_dealer.SetRoutingId("Game0"));
    XS_CHECK(target_dealer.SetRoutingId("Game1"));
    XS_CHECK(source_dealer.Connect(gate_endpoint));
    XS_CHECK(target_dealer.Connect(gate_endpoint));

    auto wait_for_single_response =
        [](RawZmqSocket& dealer, std::uint32_t expected_msg_id, std::uint32_t expected_seq) -> bool
    {
        std::vector<std::vector<std::byte>> dealer_frames;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (TryReceiveMultipartMessage(dealer.socket(), &dealer_frames))
            {
                XS_CHECK(dealer_frames.size() == 1U);
                if (dealer_frames.size() != 1U)
                {
                    return false;
                }

                xs::net::PacketView packet{};
                XS_CHECK(xs::net::DecodePacket(dealer_frames[0], &packet) == xs::net::PacketCodecErrorCode::None);
                XS_CHECK(packet.header.msg_id == expected_msg_id);
                XS_CHECK(packet.header.seq == expected_seq);
                return true;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        return false;
    };

    XS_CHECK(source_dealer.Send(EncodeRegisterRequestPacket(1U, "Game0", static_cast<std::uint16_t>(xs::net::InnerProcessType::Game), game0_inner_port)));
    XS_CHECK(target_dealer.Send(EncodeRegisterRequestPacket(1U, "Game1", static_cast<std::uint16_t>(xs::net::InnerProcessType::Game), game1_inner_port)));
    XS_CHECK(wait_for_single_response(source_dealer, xs::net::kInnerRegisterMsgId, 1U));
    XS_CHECK(wait_for_single_response(target_dealer, xs::net::kInnerRegisterMsgId, 1U));

    XS_CHECK(source_dealer.Send(EncodeHeartbeatRequestPacket(2U)));
    XS_CHECK(target_dealer.Send(EncodeHeartbeatRequestPacket(2U)));
    XS_CHECK(wait_for_single_response(source_dealer, xs::net::kInnerHeartbeatMsgId, 2U));
    XS_CHECK(wait_for_single_response(target_dealer, xs::net::kInnerHeartbeatMsgId, 2U));

    const std::string stub_payload_text = "relay-test-payload";
    const std::span<const std::byte> stub_payload(
        reinterpret_cast<const std::byte*>(stub_payload_text.data()),
        stub_payload_text.size());
    XS_CHECK(source_dealer.Send(EncodeRelayForwardStubCallPacket("Game0", "Game1", "MatchStub", 5101U, stub_payload)));

    std::vector<std::vector<std::byte>> target_frames;
    bool forwarded = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (TryReceiveMultipartMessage(target_dealer.socket(), &target_frames))
        {
            XS_CHECK(target_frames.size() == 1U);
            if (target_frames.size() != 1U)
            {
                break;
            }

            xs::net::PacketView packet{};
            XS_CHECK(xs::net::DecodePacket(target_frames[0], &packet) == xs::net::PacketCodecErrorCode::None);
            XS_CHECK(packet.header.msg_id == xs::net::kRelayForwardStubCallMsgId);
            XS_CHECK(packet.header.flags == 0U);
            XS_CHECK(packet.header.seq == xs::net::kPacketSeqNone);

            xs::net::RelayForwardStubCall relay_message{};
            XS_CHECK(
                xs::net::DecodeRelayForwardStubCall(packet.payload, &relay_message) ==
                xs::net::RelayCodecErrorCode::None);
            XS_CHECK(relay_message.source_game_node_id == "Game0");
            XS_CHECK(relay_message.target_game_node_id == "Game1");
            XS_CHECK(relay_message.target_stub_type == "MatchStub");
            XS_CHECK(relay_message.stub_call_msg_id == 5101U);
            XS_CHECK(std::string(reinterpret_cast<const char*>(relay_message.payload.data()), relay_message.payload.size()) ==
                     stub_payload_text);
            forwarded = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    XS_CHECK(forwarded);

    gate_node.StopAndJoin();
    XS_CHECK_MSG(gate_node.run_result() == xs::node::NodeErrorCode::None, gate_node.run_error().data());
    XS_CHECK(gate_node.Uninit());

    CleanupTestDirectory(base_path);
}

void TestGateNodeRejectsHeartbeatResponseWithErrorFlag()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gate-gm-session-heartbeat-invalid-envelope");

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

    RunningGateNode gate_node(config_path);
    if (!gate_node.Start())
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
                XS_CHECK(request.node_id == "Gate0");
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

        if (gate_node.run_completed())
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (invalid_heartbeat_sent)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    gate_node.StopAndJoin();
    XS_CHECK_MSG(gate_node.run_result() == xs::node::NodeErrorCode::None, gate_node.run_error().data());
    XS_CHECK(gate_node.Uninit());

    XS_CHECK(register_accepted);
    XS_CHECK(invalid_heartbeat_sent);

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Gate node accepted GM register success response.") != std::string::npos);
    XS_CHECK(log_text.find("Gate node ignored GM heartbeat response with an invalid envelope.") != std::string::npos);
    XS_CHECK(log_text.find("Gate node accepted GM heartbeat success response.") == std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestGateNodeRegistersAndRefreshesHeartbeatAgainstRealGm();
    TestGateNodeOpensClientNetworkOnlyAfterClusterReadyNotify();
    TestGateNodeAcceptsGameRegisterAndHeartbeat();
    TestGateNodeRejectsUnknownGameRegister();
    TestGateNodeForwardsStubCallBetweenGames();
    TestGateNodeRejectsHeartbeatResponseWithErrorFlag();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " Gate GM session test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

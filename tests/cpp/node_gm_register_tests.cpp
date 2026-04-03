#include "GmNode.h"
#include "Json.h"
#include "TestManagedConfigJson.h"
#include "ZmqActiveConnector.h"
#include "ZmqContext.h"
#include "message/InnerClusterCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
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

inline constexpr std::int32_t kInnerProcessTypeInvalid = 3000;
inline constexpr std::int32_t kInnerNodeIdConflict = 3001;
inline constexpr std::int32_t kInnerNetworkEndpointInvalid = 3002;
inline constexpr std::int32_t kInnerRequestInvalid = 3005;

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

std::string BuildTestServerEntityId(std::size_t index)
{
    return "00000000-0000-4000-8000-00000000000" + std::to_string(static_cast<unsigned long long>(index));
}

xs::core::Json MakeClusterConfigJson(
    const std::filesystem::path& base_path,
    std::uint16_t gm_inner_port,
    std::uint16_t gm_control_port)
{
    const std::string root_log_dir = (base_path / "logs").string();

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
                        xs::core::Json{{"host", "127.0.0.1"}, {"port", 7000}}},
                   }},
                  {"authNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "0.0.0.0"}, {"port", 4100}}},
                   }},
                  {"clientNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "0.0.0.0"}, {"port", 4000}}},
                   }},
              }},
         }},
        {"game",
         xs::core::Json{
             {"Game0",
              xs::core::Json{
                  {"innerNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "127.0.0.1"}, {"port", 7100}}},
                   }},
              }},
         }},
    };
}

bool WriteRuntimeConfig(
    const std::filesystem::path& base_path,
    std::uint16_t gm_inner_port,
    std::uint16_t gm_control_port,
    std::filesystem::path* file_path)
{
    if (file_path == nullptr)
    {
        XS_CHECK(false);
        return false;
    }

    *file_path = base_path / "config.json";
    return WriteJsonFile(*file_path, MakeClusterConfigJson(base_path, gm_inner_port, gm_control_port));
}

std::vector<std::byte> BytesFromText(std::string_view value)
{
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());

    for (const char ch : value)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }

    return bytes;
}

bool ByteSpanEqualsText(std::span<const std::byte> bytes, std::string_view text)
{
    if (bytes.size() != text.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < text.size(); ++index)
    {
        if (bytes[index] != static_cast<std::byte>(static_cast<unsigned char>(text[index])))
        {
            return false;
        }
    }

    return true;
}

bool SpinUntil(
    asio::io_context& io_context,
    std::chrono::milliseconds timeout,
    const std::function<bool()>& predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (predicate())
        {
            return true;
        }

        if (io_context.stopped())
        {
            io_context.restart();
        }

        (void)io_context.run_for(std::chrono::milliseconds(5));
    }

    return predicate();
}

xs::net::RegisterRequest MakeRegisterRequest(
    std::uint16_t process_type,
    std::string node_id,
    std::uint32_t pid,
    std::uint64_t started_at_unix_ms,
    std::uint16_t service_port)
{
    return xs::net::RegisterRequest{
        .process_type = process_type,
        .process_flags = 0u,
        .node_id = std::move(node_id),
        .pid = pid,
        .started_at_unix_ms = started_at_unix_ms,
        .inner_network_endpoint = xs::net::Endpoint{.host = "127.0.0.1", .port = service_port},
        .build_version = "test-build",
        .capability_tags = {"cluster", "inner"},
        .load = xs::net::LoadSnapshot{
            .connection_count = 1u,
            .session_count = 2u,
            .entity_count = 3u,
            .space_count = 4u,
            .load_score = 5u,
        },
    };
}

std::vector<std::byte> EncodeRegisterPayload(const xs::net::RegisterRequest& request)
{
    std::size_t payload_size = 0;
    const xs::net::RegisterCodecErrorCode size_result =
        xs::net::GetRegisterRequestWireSize(request, &payload_size);
    XS_CHECK(size_result == xs::net::RegisterCodecErrorCode::None);

    std::vector<std::byte> payload(payload_size);
    const xs::net::RegisterCodecErrorCode encode_result = xs::net::EncodeRegisterRequest(request, payload);
    XS_CHECK(encode_result == xs::net::RegisterCodecErrorCode::None);
    return payload;
}

std::vector<std::byte> BuildInnerRegisterPacket(
    std::span<const std::byte> register_payload,
    std::uint32_t seq)
{
    const xs::net::PacketHeader header =
        xs::net::MakePacketHeader(
            xs::net::kInnerRegisterMsgId,
            seq,
            0u,
            static_cast<std::uint32_t>(register_payload.size()));

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + register_payload.size());
    const xs::net::PacketCodecErrorCode encode_result =
        xs::net::EncodePacket(header, register_payload, packet);
    XS_CHECK(encode_result == xs::net::PacketCodecErrorCode::None);
    return packet;
}

xs::net::PacketView DecodeResponsePacket(
    const std::vector<std::byte>& response)
{
    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode decode_result = xs::net::DecodePacket(response, &packet);
    XS_CHECK(decode_result == xs::net::PacketCodecErrorCode::None);
    return packet;
}

const std::vector<std::byte>* FindFirstPacketByMsgId(
    const std::vector<std::vector<std::byte>>& packets,
    std::uint32_t msg_id)
{
    for (const std::vector<std::byte>& packet_bytes : packets)
    {
        xs::net::PacketView packet{};
        if (xs::net::DecodePacket(packet_bytes, &packet) != xs::net::PacketCodecErrorCode::None)
        {
            continue;
        }

        if (packet.header.msg_id == msg_id)
        {
            return &packet_bytes;
        }
    }

    return nullptr;
}

std::size_t CountPacketsByMsgId(
    const std::vector<std::vector<std::byte>>& packets,
    std::uint32_t msg_id)
{
    std::size_t count = 0U;
    for (const std::vector<std::byte>& packet_bytes : packets)
    {
        xs::net::PacketView packet{};
        if (xs::net::DecodePacket(packet_bytes, &packet) != xs::net::PacketCodecErrorCode::None)
        {
            continue;
        }

        if (packet.header.msg_id == msg_id)
        {
            ++count;
        }
    }

    return count;
}

const std::vector<std::byte>* FindLastPacketByMsgId(
    const std::vector<std::vector<std::byte>>& packets,
    std::uint32_t msg_id)
{
    for (auto iterator = packets.rbegin(); iterator != packets.rend(); ++iterator)
    {
        xs::net::PacketView packet{};
        if (xs::net::DecodePacket(*iterator, &packet) != xs::net::PacketCodecErrorCode::None)
        {
            continue;
        }

        if (packet.header.msg_id == msg_id)
        {
            return &(*iterator);
        }
    }

    return nullptr;
}

std::vector<std::byte> BuildInnerGameGateMeshReadyReportPacket(
    std::uint64_t reported_at_unix_ms,
    std::uint16_t flags = 0U,
    std::uint32_t seq = xs::net::kPacketSeqNone)
{
    const xs::net::GameGateMeshReadyReport report{
        .status_flags = 0U,
        .reported_at_unix_ms = reported_at_unix_ms,
    };

    std::array<std::byte, xs::net::kGameGateMeshReadyReportSize> body{};
    XS_CHECK(
        xs::net::EncodeGameGateMeshReadyReport(report, body) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerGameGateMeshReadyReportMsgId,
        seq,
        flags,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
}

std::vector<std::byte> BuildInnerGameServiceReadyReportPacket(
    const xs::net::GameServiceReadyReport& report,
    std::uint16_t flags = 0U,
    std::uint32_t seq = xs::net::kPacketSeqNone)
{
    std::size_t wire_size = 0U;
    XS_CHECK(
        xs::net::GetGameServiceReadyReportWireSize(report, &wire_size) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> body(wire_size);
    XS_CHECK(
        xs::net::EncodeGameServiceReadyReport(report, body) ==
        xs::net::InnerClusterCodecErrorCode::None);

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + body.size());
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerGameServiceReadyReportMsgId,
        seq,
        flags,
        static_cast<std::uint32_t>(body.size()));
    XS_CHECK(xs::net::EncodePacket(header, body, packet) == xs::net::PacketCodecErrorCode::None);
    return packet;
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

    xs::node::GmNode& node() noexcept
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

void TestGmNodeAcceptsRegisterRequestAndStoresEntry()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-success");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> responses;
    xs::ipc::ZmqActiveConnector connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-success",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    connector.SetMessageHandler([&responses](std::vector<std::byte> payload) {
        responses.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    const bool connected = SpinUntil(io_context, std::chrono::seconds(2), [&connector]() {
        return connector.state() == xs::ipc::ZmqConnectionState::Connected;
    });
    XS_CHECK(connected);

    const xs::net::RegisterRequest request =
        MakeRegisterRequest(static_cast<std::uint16_t>(xs::net::InnerProcessType::Game), "Game0", 1001u, 2002u, 7100u);
    const std::vector<std::byte> register_payload = EncodeRegisterPayload(request);
    const std::vector<std::byte> packet = BuildInnerRegisterPacket(register_payload, 42u);

    XS_CHECK(connector.Send(packet, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    const bool response_received = SpinUntil(io_context, std::chrono::seconds(2), [&responses]() {
        return FindFirstPacketByMsgId(responses, xs::net::kInnerRegisterMsgId) != nullptr;
    });
    XS_CHECK(response_received);

    const std::vector<std::byte>* register_response = FindFirstPacketByMsgId(responses, xs::net::kInnerRegisterMsgId);
    XS_CHECK(register_response != nullptr);
    if (register_response == nullptr)
    {
        connector.Stop();
        gm_node.StopAndJoin();
        XS_CHECK(gm_node.Uninit());
        CleanupTestDirectory(base_path);
        return;
    }

    const xs::net::PacketView response_packet = DecodeResponsePacket(*register_response);
    XS_CHECK(response_packet.header.msg_id == xs::net::kInnerRegisterMsgId);
    XS_CHECK(response_packet.header.seq == 42u);
    XS_CHECK(
        response_packet.header.flags ==
        static_cast<std::uint16_t>(xs::net::PacketFlag::Response));

    xs::net::RegisterSuccessResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterSuccessResponse(response_packet.payload, &response);
    XS_CHECK(decode_result == xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(response.heartbeat_interval_ms == 5000u);
    XS_CHECK(response.heartbeat_timeout_ms == 15000u);
    XS_CHECK(response.server_now_unix_ms != 0u);

    connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());

    const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
    XS_CHECK(snapshot.size() == 1u);
    XS_CHECK(snapshot.front().process_type == xs::core::ProcessType::Game);
    XS_CHECK(snapshot.front().node_id == "Game0");
    XS_CHECK(snapshot.front().pid == 1001u);
    XS_CHECK(snapshot.front().started_at_unix_ms == 2002u);
    XS_CHECK(snapshot.front().inner_network_endpoint.host == "127.0.0.1");
    XS_CHECK(snapshot.front().inner_network_endpoint.port == 7100u);
    XS_CHECK(snapshot.front().build_version == "test-build");
    XS_CHECK(snapshot.front().capability_tags == request.capability_tags);
    XS_CHECK(snapshot.front().load.connection_count == 1u);
    XS_CHECK(snapshot.front().inner_network_ready == false);
    XS_CHECK(snapshot.front().last_heartbeat_at_unix_ms != 0u);
    XS_CHECK(ByteSpanEqualsText(snapshot.front().routing_id, "gm-route-success"));

    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}

void TestGmNodeBroadcastsClusterNodesOnlineNotifyAfterExpectedNodesRegister()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-cluster-nodes-online");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> game_messages;
    std::vector<std::vector<std::byte>> gate_messages;

    xs::ipc::ZmqActiveConnector game_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-all-online-game",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    game_connector.SetMessageHandler([&game_messages](std::vector<std::byte> payload) {
        game_messages.push_back(std::move(payload));
    });

    xs::ipc::ZmqActiveConnector gate_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-all-online-gate",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    gate_connector.SetMessageHandler([&gate_messages](std::vector<std::byte> payload) {
        gate_messages.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(game_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(gate_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return game_connector.state() == xs::ipc::ZmqConnectionState::Connected &&
               gate_connector.state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    const auto count_packets = [](const std::vector<std::vector<std::byte>>& packets, std::uint32_t msg_id) {
        std::size_t count = 0u;
        for (const auto& packet_bytes : packets)
        {
            xs::net::PacketView packet{};
            if (xs::net::DecodePacket(packet_bytes, &packet) != xs::net::PacketCodecErrorCode::None)
            {
                continue;
            }

            if (packet.header.msg_id == msg_id)
            {
                ++count;
            }
        }

        return count;
    };

    XS_CHECK(game_connector.Send(
                 BuildInnerRegisterPacket(
                     EncodeRegisterPayload(
                         MakeRegisterRequest(
                             static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
                             "Game0",
                             5101u,
                             6101u,
                             7100u)),
                     51u),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return count_packets(game_messages, xs::net::kInnerRegisterMsgId) == 1u &&
               count_packets(game_messages, xs::net::kInnerClusterNodesOnlineNotifyMsgId) == 0u;
    }));

    XS_CHECK(gate_connector.Send(
                 BuildInnerRegisterPacket(
                     EncodeRegisterPayload(
                         MakeRegisterRequest(
                             static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate),
                             "Gate0",
                             5201u,
                             6201u,
                             7000u)),
                     52u),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return count_packets(gate_messages, xs::net::kInnerRegisterMsgId) == 1u &&
               count_packets(game_messages, xs::net::kInnerClusterNodesOnlineNotifyMsgId) == 1u;
    }));

    std::vector<bool> all_nodes_online_states;
    for (const std::vector<std::byte>& packet_bytes : game_messages)
    {
        xs::net::PacketView packet{};
        XS_CHECK(xs::net::DecodePacket(packet_bytes, &packet) == xs::net::PacketCodecErrorCode::None);
        if (packet.header.msg_id != xs::net::kInnerClusterNodesOnlineNotifyMsgId)
        {
            continue;
        }

        xs::net::ClusterNodesOnlineNotify notify{};
        XS_CHECK(
            xs::net::DecodeClusterNodesOnlineNotify(packet.payload, &notify) ==
            xs::net::InnerClusterCodecErrorCode::None);
        all_nodes_online_states.push_back(notify.all_nodes_online);
    }

    XS_CHECK(all_nodes_online_states.size() == 1u);
    if (all_nodes_online_states.size() == 1u)
    {
        XS_CHECK(all_nodes_online_states[0] == true);
    }

    const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
    XS_CHECK(snapshot.size() == 2u);
    for (const xs::node::InnerNetworkSession& entry : snapshot)
    {
        XS_CHECK(entry.registered);
        XS_CHECK(!entry.inner_network_ready);
        XS_CHECK(!entry.heartbeat_timed_out);
    }

    game_connector.Stop();
    gate_connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());
    XS_CHECK(gm_node.Uninit());

    CleanupTestDirectory(base_path);
}

void TestGmNodeBroadcastsOwnershipSyncAfterExpectedGamesReportMeshReady()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-ownership-sync");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> game_messages;
    std::vector<std::vector<std::byte>> gate_messages;

    xs::ipc::ZmqActiveConnector game_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-ownership-game",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    game_connector.SetMessageHandler([&game_messages](std::vector<std::byte> payload) {
        game_messages.push_back(std::move(payload));
    });

    xs::ipc::ZmqActiveConnector gate_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-ownership-gate",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    gate_connector.SetMessageHandler([&gate_messages](std::vector<std::byte> payload) {
        gate_messages.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(game_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(gate_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return game_connector.state() == xs::ipc::ZmqConnectionState::Connected &&
            gate_connector.state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    XS_CHECK(game_connector.Send(
                 BuildInnerRegisterPacket(
                     EncodeRegisterPayload(
                         MakeRegisterRequest(
                             static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
                             "Game0",
                             6101U,
                             7101U,
                             7100U)),
                     61U),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(gate_connector.Send(
                 BuildInnerRegisterPacket(
                     EncodeRegisterPayload(
                         MakeRegisterRequest(
                             static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate),
                             "Gate0",
                             6201U,
                             7201U,
                             7000U)),
                     62U),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return CountPacketsByMsgId(game_messages, xs::net::kInnerRegisterMsgId) == 1U &&
            CountPacketsByMsgId(gate_messages, xs::net::kInnerRegisterMsgId) == 1U &&
            CountPacketsByMsgId(game_messages, xs::net::kInnerClusterNodesOnlineNotifyMsgId) == 1U;
    }));
    XS_CHECK(CountPacketsByMsgId(game_messages, xs::net::kInnerServerStubOwnershipSyncMsgId) == 0U);

    const std::uint64_t first_reported_at_unix_ms = 8101U;
    XS_CHECK(game_connector.Send(
                 BuildInnerGameGateMeshReadyReportPacket(first_reported_at_unix_ms),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return CountPacketsByMsgId(game_messages, xs::net::kInnerServerStubOwnershipSyncMsgId) == 1U;
    }));

    const std::vector<std::byte>* first_sync_packet_bytes =
        FindLastPacketByMsgId(game_messages, xs::net::kInnerServerStubOwnershipSyncMsgId);
    XS_CHECK(first_sync_packet_bytes != nullptr);
    if (first_sync_packet_bytes == nullptr)
    {
        game_connector.Stop();
        gate_connector.Stop();
        gm_node.StopAndJoin();
        XS_CHECK(gm_node.Uninit());
        CleanupTestDirectory(base_path);
        return;
    }

    const xs::net::PacketView first_sync_packet = DecodeResponsePacket(*first_sync_packet_bytes);
    XS_CHECK(first_sync_packet.header.msg_id == xs::net::kInnerServerStubOwnershipSyncMsgId);
    XS_CHECK(first_sync_packet.header.flags == 0U);
    XS_CHECK(first_sync_packet.header.seq == xs::net::kPacketSeqNone);

    xs::net::ServerStubOwnershipSync first_sync{};
    XS_CHECK(
        xs::net::DecodeServerStubOwnershipSync(first_sync_packet.payload, &first_sync) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(first_sync.assignment_epoch == 1U);
    XS_CHECK(first_sync.status_flags == 0U);
    XS_CHECK(!first_sync.assignments.empty());
    XS_CHECK(first_sync.server_now_unix_ms != 0U);
    XS_CHECK(std::all_of(
        first_sync.assignments.begin(),
        first_sync.assignments.end(),
        [](const xs::net::ServerStubOwnershipEntry& assignment) {
            return assignment.entity_id == "unknown" &&
                   assignment.owner_game_node_id == "Game0" &&
                   assignment.entry_flags == 0U;
        }));
    const auto has_first_sync_assignment = [&first_sync](std::string_view entity_type) {
        return std::any_of(
            first_sync.assignments.begin(),
            first_sync.assignments.end(),
            [entity_type](const xs::net::ServerStubOwnershipEntry& assignment) {
                return assignment.entity_type == entity_type;
            });
    };
    XS_CHECK(has_first_sync_assignment("OnlineStub"));
    XS_CHECK(has_first_sync_assignment("ChatStub"));
    XS_CHECK(has_first_sync_assignment("LeaderboardStub"));
    XS_CHECK(has_first_sync_assignment("MatchStub"));

    XS_CHECK(game_connector.Send(
                 BuildInnerGameGateMeshReadyReportPacket(8102U),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    if (io_context.stopped())
    {
        io_context.restart();
    }
    (void)io_context.run_for(std::chrono::milliseconds(200));
    XS_CHECK(CountPacketsByMsgId(game_messages, xs::net::kInnerServerStubOwnershipSyncMsgId) == 1U);

    game_connector.Stop();
    gate_connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());
    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}
void TestGmNodeOpensGateOnlyAfterAllOwnedStubsReportReady()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-open-gate-after-service-ready");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> game_messages;
    std::vector<std::vector<std::byte>> gate_messages;

    xs::ipc::ZmqActiveConnector game_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-service-ready-game",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    game_connector.SetMessageHandler([&game_messages](std::vector<std::byte> payload) {
        game_messages.push_back(std::move(payload));
    });

    xs::ipc::ZmqActiveConnector gate_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-service-ready-gate",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    gate_connector.SetMessageHandler([&gate_messages](std::vector<std::byte> payload) {
        gate_messages.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(game_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(gate_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return game_connector.state() == xs::ipc::ZmqConnectionState::Connected &&
            gate_connector.state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    XS_CHECK(game_connector.Send(
                 BuildInnerRegisterPacket(
                     EncodeRegisterPayload(
                         MakeRegisterRequest(
                             static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
                             "Game0",
                             6301U,
                             7301U,
                             7100U)),
                     63U),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(gate_connector.Send(
                 BuildInnerRegisterPacket(
                     EncodeRegisterPayload(
                         MakeRegisterRequest(
                             static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate),
                             "Gate0",
                             6401U,
                             7401U,
                             7000U)),
                     64U),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return CountPacketsByMsgId(game_messages, xs::net::kInnerRegisterMsgId) == 1U &&
            CountPacketsByMsgId(gate_messages, xs::net::kInnerRegisterMsgId) == 1U &&
            CountPacketsByMsgId(game_messages, xs::net::kInnerClusterNodesOnlineNotifyMsgId) == 1U;
    }));
    XS_CHECK(CountPacketsByMsgId(game_messages, xs::net::kInnerServerStubOwnershipSyncMsgId) == 0U);
    XS_CHECK(CountPacketsByMsgId(gate_messages, xs::net::kInnerClusterReadyNotifyMsgId) == 0U);

    XS_CHECK(game_connector.Send(
                 BuildInnerGameGateMeshReadyReportPacket(8301U),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return CountPacketsByMsgId(game_messages, xs::net::kInnerServerStubOwnershipSyncMsgId) == 1U;
    }));

    const std::vector<std::byte>* sync_packet_bytes =
        FindLastPacketByMsgId(game_messages, xs::net::kInnerServerStubOwnershipSyncMsgId);
    XS_CHECK(sync_packet_bytes != nullptr);
    if (sync_packet_bytes == nullptr)
    {
        game_connector.Stop();
        gate_connector.Stop();
        gm_node.StopAndJoin();
        XS_CHECK(gm_node.Uninit());
        CleanupTestDirectory(base_path);
        return;
    }

    const xs::net::PacketView sync_packet = DecodeResponsePacket(*sync_packet_bytes);
    xs::net::ServerStubOwnershipSync sync{};
    XS_CHECK(
        xs::net::DecodeServerStubOwnershipSync(sync_packet.payload, &sync) ==
        xs::net::InnerClusterCodecErrorCode::None);
    XS_CHECK(sync.assignment_epoch == 1U);
    XS_CHECK(!sync.assignments.empty());

    std::vector<xs::net::ServerStubReadyEntry> partial_entries;
    partial_entries.reserve(sync.assignments.size());
    for (std::size_t index = 0U; index < sync.assignments.size(); ++index)
    {
        const bool should_report_ready = index + 1U != sync.assignments.size();
        partial_entries.push_back(
            xs::net::ServerStubReadyEntry{
                .entity_type = sync.assignments[index].entity_type,
                .entity_id = should_report_ready ? BuildTestServerEntityId(index) : std::string("unknown"),
                .ready = should_report_ready,
                .entry_flags = 0U,
            });
    }
    XS_CHECK(partial_entries.size() == sync.assignments.size());

    const xs::net::GameServiceReadyReport partial_report{
        .assignment_epoch = sync.assignment_epoch,
        .local_ready = true,
        .status_flags = 0U,
        .entries = partial_entries,
        .reported_at_unix_ms = 8302U,
    };
    XS_CHECK(game_connector.Send(
                 BuildInnerGameServiceReadyReportPacket(partial_report),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    if (io_context.stopped())
    {
        io_context.restart();
    }
    (void)io_context.run_for(std::chrono::milliseconds(200));
    XS_CHECK(CountPacketsByMsgId(gate_messages, xs::net::kInnerClusterReadyNotifyMsgId) == 0U);

    std::vector<xs::net::ServerStubReadyEntry> full_entries;
    full_entries.reserve(sync.assignments.size());
    for (const xs::net::ServerStubOwnershipEntry& assignment : sync.assignments)
    {
        full_entries.push_back(
            xs::net::ServerStubReadyEntry{
                .entity_type = assignment.entity_type,
                .entity_id = BuildTestServerEntityId(full_entries.size()),
                .ready = true,
                .entry_flags = 0U,
            });
    }

    const xs::net::GameServiceReadyReport full_report{
        .assignment_epoch = sync.assignment_epoch,
        .local_ready = true,
        .status_flags = 0U,
        .entries = full_entries,
        .reported_at_unix_ms = 8303U,
    };
    XS_CHECK(game_connector.Send(
                 BuildInnerGameServiceReadyReportPacket(full_report),
                 &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return CountPacketsByMsgId(gate_messages, xs::net::kInnerClusterReadyNotifyMsgId) == 1U;
    }));

    const std::vector<std::byte>* notify_packet_bytes =
        FindLastPacketByMsgId(gate_messages, xs::net::kInnerClusterReadyNotifyMsgId);
    XS_CHECK(notify_packet_bytes != nullptr);
    if (notify_packet_bytes != nullptr)
    {
        const xs::net::PacketView notify_packet = DecodeResponsePacket(*notify_packet_bytes);
        XS_CHECK(notify_packet.header.msg_id == xs::net::kInnerClusterReadyNotifyMsgId);
        XS_CHECK(notify_packet.header.flags == 0U);
        XS_CHECK(notify_packet.header.seq == xs::net::kPacketSeqNone);

        xs::net::ClusterReadyNotify notify{};
        XS_CHECK(
            xs::net::DecodeClusterReadyNotify(notify_packet.payload, &notify) ==
            xs::net::InnerClusterCodecErrorCode::None);
        XS_CHECK(notify.ready_epoch == 1U);
        XS_CHECK(notify.cluster_ready);
        XS_CHECK(notify.status_flags == 0U);
        XS_CHECK(notify.server_now_unix_ms != 0U);
    }

    game_connector.Stop();
    gate_connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());
    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}

void TestGmNodeRejectsDuplicateNodeId()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-duplicate-node");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> first_responses;
    std::vector<std::vector<std::byte>> second_responses;

    xs::ipc::ZmqActiveConnector first_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-dup-a",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    first_connector.SetMessageHandler([&first_responses](std::vector<std::byte> payload) {
        first_responses.push_back(std::move(payload));
    });

    xs::ipc::ZmqActiveConnector second_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-dup-b",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    second_connector.SetMessageHandler([&second_responses](std::vector<std::byte> payload) {
        second_responses.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(first_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(second_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    const bool connected = SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return first_connector.state() == xs::ipc::ZmqConnectionState::Connected &&
               second_connector.state() == xs::ipc::ZmqConnectionState::Connected;
    });
    XS_CHECK(connected);

    const std::vector<std::byte> first_packet = BuildInnerRegisterPacket(
        EncodeRegisterPayload(
            MakeRegisterRequest(static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate), "Gate0", 2001u, 3001u, 7000u)),
        11u);
    XS_CHECK(first_connector.Send(first_packet, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&first_responses]() {
        return first_responses.size() == 1u;
    }));

    const std::vector<std::byte> second_packet = BuildInnerRegisterPacket(
        EncodeRegisterPayload(
            MakeRegisterRequest(static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate), "Gate0", 2002u, 3002u, 7001u)),
        12u);
    XS_CHECK(second_connector.Send(second_packet, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&second_responses]() {
        return second_responses.size() == 1u;
    }));

    const xs::net::PacketView error_packet = DecodeResponsePacket(second_responses.front());
    XS_CHECK(error_packet.header.msg_id == xs::net::kInnerRegisterMsgId);
    XS_CHECK(error_packet.header.seq == 12u);
    XS_CHECK(
        error_packet.header.flags ==
        (static_cast<std::uint16_t>(xs::net::PacketFlag::Response) |
         static_cast<std::uint16_t>(xs::net::PacketFlag::Error)));

    xs::net::RegisterErrorResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterErrorResponse(error_packet.payload, &response);
    XS_CHECK(decode_result == xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(response.error_code == kInnerNodeIdConflict);
    XS_CHECK(response.retry_after_ms == 0u);

    first_connector.Stop();
    second_connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());

    const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
    XS_CHECK(snapshot.size() == 1u);
    XS_CHECK(snapshot.front().node_id == "Gate0");
    XS_CHECK(snapshot.front().pid == 2001u);
    XS_CHECK(ByteSpanEqualsText(snapshot.front().routing_id, "gm-route-dup-a"));

    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}

void TestGmNodeAcceptsGateAndGameRegistrationsSequentially()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-sequential-success");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> gate_responses;
    std::vector<std::vector<std::byte>> game_responses;

    xs::ipc::ZmqActiveConnector gate_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-gate-success",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    gate_connector.SetMessageHandler([&gate_responses](std::vector<std::byte> payload) {
        gate_responses.push_back(std::move(payload));
    });

    xs::ipc::ZmqActiveConnector game_connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-game-success",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    game_connector.SetMessageHandler([&game_responses](std::vector<std::byte> payload) {
        game_responses.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(gate_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(game_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return gate_connector.state() == xs::ipc::ZmqConnectionState::Connected &&
               game_connector.state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    const xs::net::RegisterRequest gate_request =
        MakeRegisterRequest(static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate), "Gate0", 3101u, 4101u, 7000u);
    const xs::net::RegisterRequest game_request =
        MakeRegisterRequest(static_cast<std::uint16_t>(xs::net::InnerProcessType::Game), "Game0", 3102u, 4102u, 7100u);

    XS_CHECK(gate_connector.Send(BuildInnerRegisterPacket(EncodeRegisterPayload(gate_request), 31u), &error_message) ==
             xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(game_connector.Send(BuildInnerRegisterPacket(EncodeRegisterPayload(game_request), 32u), &error_message) ==
             xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return FindFirstPacketByMsgId(gate_responses, xs::net::kInnerRegisterMsgId) != nullptr &&
               FindFirstPacketByMsgId(game_responses, xs::net::kInnerRegisterMsgId) != nullptr;
    }));

    const std::vector<std::byte>* gate_register_response =
        FindFirstPacketByMsgId(gate_responses, xs::net::kInnerRegisterMsgId);
    const std::vector<std::byte>* game_register_response =
        FindFirstPacketByMsgId(game_responses, xs::net::kInnerRegisterMsgId);
    XS_CHECK(gate_register_response != nullptr);
    XS_CHECK(game_register_response != nullptr);
    if (gate_register_response == nullptr || game_register_response == nullptr)
    {
        gate_connector.Stop();
        game_connector.Stop();
        gm_node.StopAndJoin();
        XS_CHECK(gm_node.Uninit());
        CleanupTestDirectory(base_path);
        return;
    }

    const xs::net::PacketView gate_packet = DecodeResponsePacket(*gate_register_response);
    XS_CHECK(gate_packet.header.msg_id == xs::net::kInnerRegisterMsgId);
    XS_CHECK(gate_packet.header.seq == 31u);
    XS_CHECK(gate_packet.header.flags == static_cast<std::uint16_t>(xs::net::PacketFlag::Response));

    const xs::net::PacketView game_packet = DecodeResponsePacket(*game_register_response);
    XS_CHECK(game_packet.header.msg_id == xs::net::kInnerRegisterMsgId);
    XS_CHECK(game_packet.header.seq == 32u);
    XS_CHECK(game_packet.header.flags == static_cast<std::uint16_t>(xs::net::PacketFlag::Response));

    xs::net::RegisterSuccessResponse gate_response{};
    xs::net::RegisterSuccessResponse game_response{};
    XS_CHECK(xs::net::DecodeRegisterSuccessResponse(gate_packet.payload, &gate_response) ==
             xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(xs::net::DecodeRegisterSuccessResponse(game_packet.payload, &game_response) ==
             xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(gate_response.heartbeat_interval_ms == 5000u);
    XS_CHECK(gate_response.heartbeat_timeout_ms == 15000u);
    XS_CHECK(game_response.heartbeat_interval_ms == 5000u);
    XS_CHECK(game_response.heartbeat_timeout_ms == 15000u);

    gate_connector.Stop();
    game_connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());

    const std::vector<xs::node::InnerNetworkSession> snapshot = gm_node.node().registry_snapshot();
    XS_CHECK(snapshot.size() == 2u);

    const auto gate_entry = std::find_if(snapshot.begin(), snapshot.end(), [](const xs::node::InnerNetworkSession& entry) {
        return entry.node_id == "Gate0";
    });
    XS_CHECK(gate_entry != snapshot.end());
    if (gate_entry != snapshot.end())
    {
        XS_CHECK(gate_entry->process_type == xs::core::ProcessType::Gate);
        XS_CHECK(gate_entry->inner_network_endpoint.port == 7000u);
        XS_CHECK(ByteSpanEqualsText(gate_entry->routing_id, "gm-route-gate-success"));
    }

    const auto game_entry = std::find_if(snapshot.begin(), snapshot.end(), [](const xs::node::InnerNetworkSession& entry) {
        return entry.node_id == "Game0";
    });
    XS_CHECK(game_entry != snapshot.end());
    if (game_entry != snapshot.end())
    {
        XS_CHECK(game_entry->process_type == xs::core::ProcessType::Game);
        XS_CHECK(game_entry->inner_network_endpoint.port == 7100u);
        XS_CHECK(ByteSpanEqualsText(game_entry->routing_id, "gm-route-game-success"));
    }

    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}

void TestGmNodeRejectsInvalidProcessType()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-invalid-process-type");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> responses;
    xs::ipc::ZmqActiveConnector connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-invalid-process-type",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    connector.SetMessageHandler([&responses](std::vector<std::byte> payload) {
        responses.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&connector]() {
        return connector.state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    std::vector<std::byte> payload = EncodeRegisterPayload(
        MakeRegisterRequest(static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate), "Gate3", 2401u, 3401u, 7003u));
    // `processType` lives at bytes [0..1] in the stable M2-12 register payload layout.
    payload[0] = std::byte{0x00};
    payload[1] = std::byte{0x00};

    const std::vector<std::byte> packet = BuildInnerRegisterPacket(payload, 23u);
    XS_CHECK(connector.Send(packet, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&responses]() {
        return responses.size() == 1u;
    }));

    const xs::net::PacketView error_packet = DecodeResponsePacket(responses.front());
    XS_CHECK(error_packet.header.seq == 23u);
    XS_CHECK(
        error_packet.header.flags ==
        (static_cast<std::uint16_t>(xs::net::PacketFlag::Response) |
         static_cast<std::uint16_t>(xs::net::PacketFlag::Error)));

    xs::net::RegisterErrorResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterErrorResponse(error_packet.payload, &response);
    XS_CHECK(decode_result == xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(response.error_code == kInnerProcessTypeInvalid);
    XS_CHECK(response.retry_after_ms == 0u);

    connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK(gm_node.node().registry_snapshot().empty());
    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}

void TestGmNodeRejectsInvalidInnerNetworkEndpoint()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-invalid-endpoint");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> responses;
    xs::ipc::ZmqActiveConnector connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-invalid-endpoint",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    connector.SetMessageHandler([&responses](std::vector<std::byte> payload) {
        responses.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&connector]() {
        return connector.state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    std::vector<std::byte> payload = EncodeRegisterPayload(
        MakeRegisterRequest(static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate), "Gate1", 2201u, 3201u, 7001u));
    // `innerNetworkEndpoint.port` lives at bytes [34..35] for the fixed request shape used in this test.
    payload[34] = std::byte{0x00};
    payload[35] = std::byte{0x00};

    const std::vector<std::byte> packet = BuildInnerRegisterPacket(payload, 21u);
    XS_CHECK(connector.Send(packet, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&responses]() {
        return responses.size() == 1u;
    }));

    const xs::net::PacketView error_packet = DecodeResponsePacket(responses.front());
    XS_CHECK(error_packet.header.seq == 21u);
    XS_CHECK(
        error_packet.header.flags ==
        (static_cast<std::uint16_t>(xs::net::PacketFlag::Response) |
         static_cast<std::uint16_t>(xs::net::PacketFlag::Error)));

    xs::net::RegisterErrorResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterErrorResponse(error_packet.payload, &response);
    XS_CHECK(decode_result == xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(response.error_code == kInnerNetworkEndpointInvalid);

    connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK(gm_node.node().registry_snapshot().empty());
    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}

void TestGmNodeRejectsInvalidRegisterPayload()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-invalid-payload");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> responses;
    xs::ipc::ZmqActiveConnector connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-invalid-payload",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    connector.SetMessageHandler([&responses](std::vector<std::byte> payload) {
        responses.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&connector]() {
        return connector.state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    std::vector<std::byte> payload = EncodeRegisterPayload(
        MakeRegisterRequest(static_cast<std::uint16_t>(xs::net::InnerProcessType::Gate), "Gate2", 2301u, 3301u, 7002u));
    // `processFlags` lives at bytes [2..3] in the stable M2-12 register payload layout.
    payload[3] = std::byte{0x01};

    const std::vector<std::byte> packet = BuildInnerRegisterPacket(payload, 22u);
    XS_CHECK(connector.Send(packet, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&responses]() {
        return responses.size() == 1u;
    }));

    const xs::net::PacketView error_packet = DecodeResponsePacket(responses.front());
    XS_CHECK(error_packet.header.seq == 22u);
    XS_CHECK(
        error_packet.header.flags ==
        (static_cast<std::uint16_t>(xs::net::PacketFlag::Response) |
         static_cast<std::uint16_t>(xs::net::PacketFlag::Error)));

    xs::net::RegisterErrorResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterErrorResponse(error_packet.payload, &response);
    XS_CHECK(decode_result == xs::net::RegisterCodecErrorCode::None);
    XS_CHECK(response.error_code == kInnerRequestInvalid);

    connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK(gm_node.node().registry_snapshot().empty());
    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}

void TestGmNodeDropsMalformedPacketWithoutResponse()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-malformed-packet");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, AcquireLoopbackPort(), &config_path))
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

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<std::vector<std::byte>> responses;
    xs::ipc::ZmqActiveConnector connector(
        io_context,
        context,
        {
            .remote_endpoint = "tcp://127.0.0.1:" + std::to_string(inner_port),
            .routing_id = "gm-route-malformed",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    connector.SetMessageHandler([&responses](std::vector<std::byte> payload) {
        responses.push_back(std::move(payload));
    });

    std::string error_message;
    XS_CHECK(connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(SpinUntil(io_context, std::chrono::seconds(2), [&connector]() {
        return connector.state() == xs::ipc::ZmqConnectionState::Connected;
    }));

    const std::vector<std::byte> malformed_packet = BytesFromText("bad-packet");
    XS_CHECK(connector.Send(malformed_packet, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    if (io_context.stopped())
    {
        io_context.restart();
    }
    (void)io_context.run_for(std::chrono::milliseconds(300));

    XS_CHECK(responses.empty());

    connector.Stop();
    gm_node.StopAndJoin();
    XS_CHECK(gm_node.node().registry_snapshot().empty());
    XS_CHECK(gm_node.Uninit());
    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestGmNodeAcceptsRegisterRequestAndStoresEntry();
    TestGmNodeBroadcastsClusterNodesOnlineNotifyAfterExpectedNodesRegister();
    TestGmNodeBroadcastsOwnershipSyncAfterExpectedGamesReportMeshReady();
    TestGmNodeOpensGateOnlyAfterAllOwnedStubsReportReady();
    TestGmNodeRejectsDuplicateNodeId();
    TestGmNodeAcceptsGateAndGameRegistrationsSequentially();
    TestGmNodeRejectsInvalidProcessType();
    TestGmNodeRejectsInvalidInnerNetworkEndpoint();
    TestGmNodeRejectsInvalidRegisterPayload();
    TestGmNodeDropsMalformedPacketWithoutResponse();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " GM register test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


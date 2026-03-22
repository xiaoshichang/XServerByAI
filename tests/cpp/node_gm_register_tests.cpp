#include "GmNode.h"
#include "Json.h"
#include "ZmqActiveConnector.h"
#include "ZmqContext.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>

#include <algorithm>
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

xs::core::Json MakeClusterConfigJson(
    const std::filesystem::path& base_path,
    std::uint16_t gm_inner_port)
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
        {"gm",
         xs::core::Json{
             {"innerNetwork",
              xs::core::Json{
                  {"listenEndpoint",
                   xs::core::Json{{"host", "127.0.0.1"}, {"port", gm_inner_port}}},
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
                  {"managed",
                   xs::core::Json{{"assemblyName", "XServer.Managed.GameLogic"}}},
              }},
         }},
    };
}

bool WriteRuntimeConfig(
    const std::filesystem::path& base_path,
    std::uint16_t gm_inner_port,
    std::filesystem::path* file_path)
{
    if (file_path == nullptr)
    {
        XS_CHECK(false);
        return false;
    }

    *file_path = base_path / "config.json";
    return WriteJsonFile(*file_path, MakeClusterConfigJson(base_path, gm_inner_port));
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
    if (!WriteRuntimeConfig(base_path, inner_port, &config_path))
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
        return responses.size() == 1u;
    });
    XS_CHECK(response_received);
    XS_CHECK(responses.size() == 1u);

    const xs::net::PacketView response_packet = DecodeResponsePacket(responses.front());
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

    const std::vector<xs::node::ProcessRegistryEntry> snapshot = gm_node.node().registry_snapshot();
    XS_CHECK(snapshot.size() == 1u);
    XS_CHECK(snapshot.front().process_type == xs::net::InnerProcessType::Game);
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

void TestGmNodeRejectsDuplicateNodeId()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-register-duplicate-node");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, inner_port, &config_path))
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

    const std::vector<xs::node::ProcessRegistryEntry> snapshot = gm_node.node().registry_snapshot();
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
    if (!WriteRuntimeConfig(base_path, inner_port, &config_path))
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
        return gate_responses.size() == 1u && game_responses.size() == 1u;
    }));

    const xs::net::PacketView gate_packet = DecodeResponsePacket(gate_responses.front());
    XS_CHECK(gate_packet.header.msg_id == xs::net::kInnerRegisterMsgId);
    XS_CHECK(gate_packet.header.seq == 31u);
    XS_CHECK(gate_packet.header.flags == static_cast<std::uint16_t>(xs::net::PacketFlag::Response));

    const xs::net::PacketView game_packet = DecodeResponsePacket(game_responses.front());
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

    const std::vector<xs::node::ProcessRegistryEntry> snapshot = gm_node.node().registry_snapshot();
    XS_CHECK(snapshot.size() == 2u);

    const auto gate_entry = std::find_if(snapshot.begin(), snapshot.end(), [](const xs::node::ProcessRegistryEntry& entry) {
        return entry.node_id == "Gate0";
    });
    XS_CHECK(gate_entry != snapshot.end());
    if (gate_entry != snapshot.end())
    {
        XS_CHECK(gate_entry->process_type == xs::net::InnerProcessType::Gate);
        XS_CHECK(gate_entry->inner_network_endpoint.port == 7000u);
        XS_CHECK(ByteSpanEqualsText(gate_entry->routing_id, "gm-route-gate-success"));
    }

    const auto game_entry = std::find_if(snapshot.begin(), snapshot.end(), [](const xs::node::ProcessRegistryEntry& entry) {
        return entry.node_id == "Game0";
    });
    XS_CHECK(game_entry != snapshot.end());
    if (game_entry != snapshot.end())
    {
        XS_CHECK(game_entry->process_type == xs::net::InnerProcessType::Game);
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
    if (!WriteRuntimeConfig(base_path, inner_port, &config_path))
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
    if (!WriteRuntimeConfig(base_path, inner_port, &config_path))
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
    if (!WriteRuntimeConfig(base_path, inner_port, &config_path))
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
    if (!WriteRuntimeConfig(base_path, inner_port, &config_path))
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


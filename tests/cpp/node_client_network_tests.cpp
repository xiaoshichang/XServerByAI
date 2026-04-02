#include "ClientNetwork.h"

#include "ClientSession.h"
#include "KcpPeer.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/udp.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
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

xs::core::LoggerOptions MakeLoggerOptions(const std::filesystem::path& root_dir)
{
    xs::core::LoggerOptions options;
    options.process_type = xs::core::ProcessType::Gate;
    options.instance_id = "Gate0";
    options.config.root_dir = root_dir.string();
    options.config.min_level = xs::core::LogLevel::Info;
    return options;
}

xs::net::Endpoint MakeEndpoint(std::string host, std::uint16_t port)
{
    return xs::net::Endpoint{
        .host = std::move(host),
        .port = port,
    };
}

std::uint16_t AcquireLoopbackUdpPort()
{
    asio::io_context io_context;
    asio::ip::udp::socket socket(io_context, asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0u));
    const std::uint16_t port = socket.local_endpoint().port();
    socket.close();
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

bool TryReadKcpConversation(std::span<const std::byte> datagram, std::uint32_t* conversation) noexcept
{
    if (conversation == nullptr || datagram.size() < 4U)
    {
        return false;
    }

    *conversation =
        static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[0])) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[1])) << 8U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[2])) << 16U) |
        (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(datagram[3])) << 24U);
    return true;
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

bool TryReceiveDatagram(asio::ip::udp::socket& socket, std::vector<std::byte>* datagram)
{
    if (datagram == nullptr)
    {
        return false;
    }

    std::array<std::byte, 2048> buffer{};
    asio::ip::udp::endpoint remote;
    std::error_code error_code;
    const std::size_t received = socket.receive_from(asio::buffer(buffer), remote, 0, error_code);
    if (error_code == asio::error::would_block || error_code == asio::error::try_again)
    {
        return false;
    }

    XS_CHECK_MSG(!error_code, error_code.message().c_str());
    if (error_code)
    {
        return false;
    }

    datagram->assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(received));
    return true;
}

void TestClientNetworkCreatesIndexesAndRemovesSessions()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-client-network-indexes");
    const std::filesystem::path log_dir = base_path / "logs";

    xs::core::Logger logger(MakeLoggerOptions(log_dir));
    xs::core::MainEventLoop event_loop({.thread_name = "node-client-network-tests"});

    xs::node::ClientNetworkOptions options;
    options.owner_node_id = "Gate0";
    options.listen_endpoint = "127.0.0.1:4000";

    xs::node::ClientNetwork network(event_loop, logger, options);
    XS_CHECK_MSG(network.Init() == xs::node::NodeErrorCode::None, network.last_error_message().data());
    XS_CHECK(network.initialized());
    XS_CHECK(network.session_count() == 0U);

    const xs::net::Endpoint endpoint_one = MakeEndpoint("127.0.0.1", 50000U);
    const xs::net::Endpoint endpoint_two = MakeEndpoint("127.0.0.1", 50001U);
    const xs::net::Endpoint endpoint_three = MakeEndpoint("127.0.0.1", 50002U);

    std::uint64_t session_id_1 = 0U;
    XS_CHECK(network.CreateSession(11U, endpoint_one, &session_id_1, 100U) == xs::node::NodeErrorCode::None);
    XS_CHECK(session_id_1 == 1U);

    const xs::node::ClientSession* first_session = network.FindSession(session_id_1);
    XS_CHECK(first_session != nullptr);
    if (first_session != nullptr)
    {
        const xs::node::ClientSessionSnapshot snapshot = first_session->snapshot();
        XS_CHECK(snapshot.gate_node_id == "Gate0");
        XS_CHECK(snapshot.conversation == 11U);
        XS_CHECK(snapshot.connected_at_unix_ms == 100U);
    }

    std::uint64_t session_id_2 = 0U;
    XS_CHECK(network.CreateSession(12U, endpoint_two, &session_id_2, 200U) == xs::node::NodeErrorCode::None);
    XS_CHECK(session_id_2 == 2U);
    XS_CHECK(network.FindSessionByTransport(12U, endpoint_two) != nullptr);

    XS_CHECK(
        network.CreateSession(12U, endpoint_two, nullptr, 300U) ==
        xs::node::NodeErrorCode::InvalidArgument);

    std::uint64_t session_id_3 = 0U;
    XS_CHECK(network.CreateSession(12U, endpoint_three, &session_id_3, 400U) == xs::node::NodeErrorCode::None);
    XS_CHECK(session_id_3 == 3U);
    XS_CHECK(network.session_count() == 3U);

    XS_CHECK(network.RemoveSession(session_id_1));
    XS_CHECK(network.FindSession(session_id_1) == nullptr);
    XS_CHECK(!network.RemoveSession(session_id_1));

    XS_CHECK(network.Uninit() == xs::node::NodeErrorCode::None);
    XS_CHECK(!network.initialized());

    logger.Flush();
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Client network initialized.") != std::string::npos);
    XS_CHECK(log_text.find("Client session created.") != std::string::npos);
    XS_CHECK(log_text.find("Client session removed.") != std::string::npos);

    CleanupTestDirectory(base_path);
}
void TestClientNetworkReceivesUdpAndCreatesSessionsByTransportKey()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-client-network-udp-kcp");
    const std::filesystem::path log_dir = base_path / "logs";

    xs::core::Logger logger(MakeLoggerOptions(log_dir));
    xs::core::MainEventLoop event_loop({.thread_name = "node-client-network-runtime-tests"});

    xs::node::ClientNetworkOptions options;
    options.owner_node_id = "Gate0";
    options.listen_endpoint = "127.0.0.1:" + std::to_string(AcquireLoopbackUdpPort());
    options.kcp.no_congestion_window = true;

    xs::node::ClientNetwork network(event_loop, logger, options);
    XS_CHECK_MSG(network.Init() == xs::node::NodeErrorCode::None, network.last_error_message().data());
    XS_CHECK_MSG(network.Run() == xs::node::NodeErrorCode::None, network.last_error_message().data());
    XS_CHECK(network.running());

    std::atomic_bool loop_started{false};
    xs::core::MainEventLoopErrorCode loop_result = xs::core::MainEventLoopErrorCode::None;
    std::string loop_error;
    std::thread loop_thread([&]() {
        xs::core::MainEventLoopHooks hooks;
        hooks.on_start = [&loop_started](xs::core::MainEventLoop&, std::string*) {
            loop_started.store(true);
            return xs::core::MainEventLoopErrorCode::None;
        };
        loop_result = event_loop.Run(std::move(hooks), &loop_error);
    });
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&loop_started]() {
        return loop_started.load();
    }));

    asio::io_context client_io_context;
    const asio::ip::udp::endpoint server_endpoint(
        asio::ip::address_v4::loopback(),
        static_cast<std::uint16_t>(std::stoul(std::string(options.listen_endpoint.substr(options.listen_endpoint.rfind(':') + 1U)))));

    asio::ip::udp::socket client_socket_one(
        client_io_context,
        asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0U));
    client_socket_one.non_blocking(true);

    xs::net::KcpPeer client_peer_one({
        .conversation = 11U,
        .config = options.kcp,
    });
    XS_CHECK_MSG(client_peer_one.valid(), client_peer_one.last_error_message().data());
    XS_CHECK(client_peer_one.Send(BytesFromText("hello-one")) == xs::net::KcpPeerErrorCode::None);
    XS_CHECK(client_peer_one.Flush(0U) == xs::net::KcpPeerErrorCode::None);
    SendDatagrams(client_socket_one, server_endpoint, client_peer_one.ConsumeOutgoingDatagrams());

    const xs::net::Endpoint client_endpoint_one = MakeEndpoint(
        client_socket_one.local_endpoint().address().to_string(),
        client_socket_one.local_endpoint().port());
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&network, &client_endpoint_one]() {
        return network.session_count() == 1U && network.FindSessionByTransport(11U, client_endpoint_one) != nullptr;
    }));

    const xs::node::ClientSession* first_session = network.FindSessionByTransport(11U, client_endpoint_one);
    XS_CHECK(first_session != nullptr);
    std::uint64_t first_session_id = 0U;
    if (first_session != nullptr)
    {
        first_session_id = first_session->session_id();
        XS_CHECK(first_session->session_state() == xs::node::ClientSessionState::Created);
        XS_CHECK(first_session->route_state() == xs::node::ClientRouteState::Unassigned);
    }

    std::vector<std::byte> ack_datagram;
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&]() {
        return TryReceiveDatagram(client_socket_one, &ack_datagram);
    }));
    std::uint32_t ack_conversation = 0U;
    XS_CHECK(TryReadKcpConversation(ack_datagram, &ack_conversation));
    XS_CHECK(ack_conversation == 11U);

    XS_CHECK(client_peer_one.Send(BytesFromText("hello-again")) == xs::net::KcpPeerErrorCode::None);
    XS_CHECK(client_peer_one.Flush(1U) == xs::net::KcpPeerErrorCode::None);
    SendDatagrams(client_socket_one, server_endpoint, client_peer_one.ConsumeOutgoingDatagrams());
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&network]() {
        return network.session_count() == 1U;
    }));

    asio::ip::udp::socket client_socket_two(
        client_io_context,
        asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0U));
    client_socket_two.non_blocking(true);

    xs::net::KcpPeer client_peer_two({
        .conversation = 11U,
        .config = options.kcp,
    });
    XS_CHECK_MSG(client_peer_two.valid(), client_peer_two.last_error_message().data());
    XS_CHECK(client_peer_two.Send(BytesFromText("hello-two")) == xs::net::KcpPeerErrorCode::None);
    XS_CHECK(client_peer_two.Flush(0U) == xs::net::KcpPeerErrorCode::None);
    SendDatagrams(client_socket_two, server_endpoint, client_peer_two.ConsumeOutgoingDatagrams());

    const xs::net::Endpoint client_endpoint_two = MakeEndpoint(
        client_socket_two.local_endpoint().address().to_string(),
        client_socket_two.local_endpoint().port());
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&network, &client_endpoint_two]() {
        return network.session_count() == 2U && network.FindSessionByTransport(11U, client_endpoint_two) != nullptr;
    }));

    const xs::node::ClientSession* second_session = network.FindSessionByTransport(11U, client_endpoint_two);
    XS_CHECK(second_session != nullptr);
    if (second_session != nullptr)
    {
        XS_CHECK(second_session->session_id() != first_session_id);
    }

    XS_CHECK(network.Stop() == xs::node::NodeErrorCode::None);
    XS_CHECK(!network.running());

    asio::ip::udp::socket client_socket_three(
        client_io_context,
        asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0U));
    xs::net::KcpPeer client_peer_three({
        .conversation = 22U,
        .config = options.kcp,
    });
    XS_CHECK(client_peer_three.Send(BytesFromText("after-stop")) == xs::net::KcpPeerErrorCode::None);
    XS_CHECK(client_peer_three.Flush(0U) == xs::net::KcpPeerErrorCode::None);
    SendDatagrams(client_socket_three, server_endpoint, client_peer_three.ConsumeOutgoingDatagrams());
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    XS_CHECK(network.session_count() == 2U);

    event_loop.RequestStop();
    loop_thread.join();
    XS_CHECK_MSG(loop_result == xs::core::MainEventLoopErrorCode::None, loop_error.c_str());

    XS_CHECK(network.Uninit() == xs::node::NodeErrorCode::None);

    logger.Flush();
    XS_CHECK(DirectoryContainsRegularFile(log_dir));
    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Client network started.") != std::string::npos);
    XS_CHECK(log_text.find("Client network stopped.") != std::string::npos);
    XS_CHECK(log_text.find("Client network received a client payload.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestClientNetworkCreatesIndexesAndRemovesSessions();
    TestClientNetworkReceivesUdpAndCreatesSessionsByTransportKey();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " client network test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include "InnerNetwork.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>

#include <zmq.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
        return socket_ != nullptr && zmq_connect(socket_, std::string(endpoint).c_str()) == 0;
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

xs::core::LoggerOptions MakeLoggerOptions(
    const std::filesystem::path& root_dir,
    xs::core::ProcessType process_type,
    std::string instance_id)
{
    xs::core::LoggerOptions options;
    options.process_type = process_type;
    options.instance_id = std::move(instance_id);
    options.config.root_dir = root_dir.string();
    options.config.min_level = xs::core::LogLevel::Info;
    return options;
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

bool ByteSpanEqualsSpan(std::span<const std::byte> left, std::span<const std::byte> right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        if (left[index] != right[index])
        {
            return false;
        }
    }

    return true;
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

bool TryReceiveSingleFrame(void* socket, std::vector<std::byte>* frame)
{
    if (socket == nullptr || frame == nullptr)
    {
        return false;
    }

    zmq_msg_t message;
    zmq_msg_init(&message);
    const int receive_result = zmq_msg_recv(&message, socket, ZMQ_DONTWAIT);
    if (receive_result < 0)
    {
        const int error_code = zmq_errno();
        zmq_msg_close(&message);
        if (error_code == EAGAIN)
        {
            return false;
        }

        XS_CHECK_MSG(false, zmq_strerror(error_code));
        return false;
    }

    if (zmq_msg_more(&message) != 0)
    {
        XS_CHECK_MSG(false, "Expected a single-frame ZeroMQ message.");
        zmq_msg_close(&message);
        return false;
    }

    const auto* data = static_cast<const std::byte*>(zmq_msg_data(&message));
    const std::size_t size = zmq_msg_size(&message);
    frame->assign(data, data + size);
    zmq_msg_close(&message);
    return true;
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

bool SendSingleFrame(void* socket, std::span<const std::byte> payload)
{
    const void* payload_data = payload.empty() ? static_cast<const void*>("") : static_cast<const void*>(payload.data());
    if (zmq_send(socket, payload_data, payload.size(), ZMQ_DONTWAIT) < 0)
    {
        const int error_code = zmq_errno();
        if (error_code == EAGAIN)
        {
            return false;
        }

        XS_CHECK_MSG(false, zmq_strerror(error_code));
        return false;
    }

    return true;
}

void TestInnerNetworkSupportsListenerAndMultipleConnectors()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-inner-network-multi-role");
    const std::filesystem::path log_dir = base_path / "logs";

    RawZmqSocket gm_router(ZMQ_ROUTER);
    RawZmqSocket gate_router(ZMQ_ROUTER);
    RawZmqSocket dealer(ZMQ_DEALER);
    XS_CHECK(gm_router.IsValid());
    XS_CHECK(gate_router.IsValid());
    XS_CHECK(dealer.IsValid());

    const std::string gm_remote_endpoint = gm_router.BindLoopbackTcp();
    const std::string gate_remote_endpoint = gate_router.BindLoopbackTcp();
    XS_CHECK(!gm_remote_endpoint.empty());
    XS_CHECK(!gate_remote_endpoint.empty());

    const std::string local_endpoint = "tcp://127.0.0.1:" + std::to_string(AcquireLoopbackPort());

    xs::core::Logger logger(MakeLoggerOptions(log_dir, xs::core::ProcessType::Game, "Game0"));
    xs::core::MainEventLoop event_loop({.thread_name = "node-inner-network-multi-role"});

    xs::node::InnerNetworkOptions options;
    options.local_endpoint = local_endpoint;
    options.connectors.push_back(
        {
            .id = "GM",
            .remote_endpoint = gm_remote_endpoint,
            .routing_id = "Game0",
        });
    options.connectors.push_back(
        {
            .id = "Gate0",
            .remote_endpoint = gate_remote_endpoint,
            .routing_id = "Game0",
        });

    auto inner_network = std::make_shared<xs::node::InnerNetwork>(event_loop, logger, std::move(options));
    const std::vector<std::byte> gm_outbound_payload = BytesFromText("game-to-gm");
    const std::vector<std::byte> gate_outbound_payload = BytesFromText("game-to-gate");
    const std::vector<std::byte> gm_reply_payload = BytesFromText("gm-reply");
    const std::vector<std::byte> gate_reply_payload = BytesFromText("gate-reply");
    const std::vector<std::byte> listener_payload = BytesFromText("game-joined");
    const std::vector<std::byte> listener_reply_payload = BytesFromText("gate-accepted");

    std::unordered_map<std::string, std::vector<std::vector<std::byte>>> connector_messages;
    std::vector<std::vector<std::byte>> gm_router_frames;
    std::vector<std::vector<std::byte>> gate_router_frames;
    std::vector<std::byte> listener_reply;
    std::vector<std::byte> listener_received_payload;
    std::vector<std::byte> listener_routing_id;
    bool gm_connected = false;
    bool gate_connected = false;
    bool gm_sent = false;
    bool gate_sent = false;
    bool gm_replied = false;
    bool gate_replied = false;
    bool dealer_connected = false;
    bool dealer_sent = false;
    bool listener_replied = false;
    bool had_listener = false;
    std::size_t configured_connector_count = 0U;
    std::string configured_gm_remote_endpoint;
    std::string configured_gate_remote_endpoint;

    inner_network->SetConnectorMessageHandler(
        [&connector_messages](std::string_view connector_id, std::vector<std::byte> payload) {
            connector_messages[std::string(connector_id)].push_back(std::move(payload));
        });
    inner_network->SetListenerMessageHandler(
        [&listener_routing_id, &listener_received_payload, &listener_replied, &listener_reply_payload, inner_network](
            std::vector<std::byte> routing_id,
            std::vector<std::byte> payload) {
            listener_routing_id = routing_id;
            listener_received_payload = payload;

            if (!listener_replied)
            {
                const xs::node::NodeErrorCode send_result = inner_network->Send(routing_id, listener_reply_payload);
                XS_CHECK_MSG(send_result == xs::node::NodeErrorCode::None, inner_network->last_error_message().data());
                listener_replied = send_result == xs::node::NodeErrorCode::None;
            }
        });

    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [&](xs::core::MainEventLoop& running_loop, std::string* error_message) {
        const xs::node::NodeErrorCode init_result = inner_network->Init();
        if (init_result != xs::node::NodeErrorCode::None)
        {
            if (error_message != nullptr)
            {
                *error_message = std::string(inner_network->last_error_message());
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        const xs::node::NodeErrorCode run_result = inner_network->Run();
        if (run_result != xs::node::NodeErrorCode::None)
        {
            if (error_message != nullptr)
            {
                *error_message = std::string(inner_network->last_error_message());
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        had_listener = inner_network->HasListener();
        configured_connector_count = inner_network->connector_count();
        configured_gm_remote_endpoint = std::string(inner_network->remote_endpoint("GM"));
        configured_gate_remote_endpoint = std::string(inner_network->remote_endpoint("Gate0"));

        const xs::core::TimerCreateResult poll_timer =
            running_loop.timers().CreateRepeating(
                std::chrono::milliseconds(5),
                [&running_loop,
                 &gm_router,
                 &gate_router,
                 &dealer,
                 &gm_router_frames,
                 &gate_router_frames,
                 &listener_reply,
                 &gm_connected,
                 &gate_connected,
                 &gm_sent,
                 &gate_sent,
                 &gm_replied,
                 &gate_replied,
                 &dealer_connected,
                 &dealer_sent,
                 &listener_replied,
                 &connector_messages,
                 &gm_outbound_payload,
                 &gate_outbound_payload,
                 &gm_reply_payload,
                 &gate_reply_payload,
                 &listener_payload,
                 inner_network]() {
                    if (!gm_connected &&
                        inner_network->connection_state("GM") == xs::ipc::ZmqConnectionState::Connected)
                    {
                        gm_connected = true;
                    }

                    if (!gate_connected &&
                        inner_network->connection_state("Gate0") == xs::ipc::ZmqConnectionState::Connected)
                    {
                        gate_connected = true;
                    }

                    if (gm_connected && !gm_sent)
                    {
                        const xs::node::NodeErrorCode send_result =
                            inner_network->SendToConnector("GM", gm_outbound_payload);
                        XS_CHECK_MSG(send_result == xs::node::NodeErrorCode::None, inner_network->last_error_message().data());
                        gm_sent = send_result == xs::node::NodeErrorCode::None;
                    }

                    if (gate_connected && !gate_sent)
                    {
                        const xs::node::NodeErrorCode send_result =
                            inner_network->SendToConnector("Gate0", gate_outbound_payload);
                        XS_CHECK_MSG(send_result == xs::node::NodeErrorCode::None, inner_network->last_error_message().data());
                        gate_sent = send_result == xs::node::NodeErrorCode::None;
                    }

                    if (!dealer_connected && inner_network->listener_state() == xs::ipc::ZmqListenerState::Listening)
                    {
                        dealer_connected = dealer.Connect(inner_network->bound_endpoint());
                        XS_CHECK(dealer_connected);
                    }

                    if (dealer_connected && !dealer_sent)
                    {
                        dealer_sent = SendSingleFrame(dealer.socket(), listener_payload);
                    }

                    if (gm_sent && !gm_replied && TryReceiveMultipartMessage(gm_router.socket(), &gm_router_frames))
                    {
                        XS_CHECK(gm_router_frames.size() == 2u);
                        if (gm_router_frames.size() == 2u)
                        {
                            XS_CHECK(SendRouterReply(gm_router.socket(), gm_router_frames[0], gm_reply_payload));
                        }

                        gm_replied = true;
                    }

                    if (gate_sent && !gate_replied && TryReceiveMultipartMessage(gate_router.socket(), &gate_router_frames))
                    {
                        XS_CHECK(gate_router_frames.size() == 2u);
                        if (gate_router_frames.size() == 2u)
                        {
                            XS_CHECK(SendRouterReply(gate_router.socket(), gate_router_frames[0], gate_reply_payload));
                        }

                        gate_replied = true;
                    }

                    if (listener_replied && listener_reply.empty())
                    {
                        (void)TryReceiveSingleFrame(dealer.socket(), &listener_reply);
                    }

                    if (gm_replied && gate_replied &&
                        connector_messages["GM"].size() == 1u &&
                        connector_messages["Gate0"].size() == 1u &&
                        !listener_reply.empty())
                    {
                        running_loop.RequestStop();
                    }
                });
        if (!xs::core::IsTimerID(poll_timer))
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to create inner network poll timer.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        const xs::core::TimerCreateResult timeout_timer =
            running_loop.timers().CreateOnce(std::chrono::seconds(3), [&running_loop]() {
                running_loop.RequestStop();
            });
        if (!xs::core::IsTimerID(timeout_timer))
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to create inner network timeout timer.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        return xs::core::MainEventLoopErrorCode::None;
    };
    hooks.on_stop = [&](xs::core::MainEventLoop&) {
        (void)inner_network->Uninit();
    };

    std::string error_message;
    const xs::core::MainEventLoopErrorCode run_result = event_loop.Run(std::move(hooks), &error_message);
    XS_CHECK_MSG(run_result == xs::core::MainEventLoopErrorCode::None, error_message.c_str());

    logger.Flush();

    XS_CHECK(had_listener);
    XS_CHECK(configured_connector_count == 2u);
    XS_CHECK(gm_connected);
    XS_CHECK(gate_connected);
    XS_CHECK(gm_sent);
    XS_CHECK(gate_sent);
    XS_CHECK(gm_replied);
    XS_CHECK(gate_replied);
    XS_CHECK(listener_replied);
    XS_CHECK(gm_router_frames.size() == 2u);
    XS_CHECK(gate_router_frames.size() == 2u);
    if (gm_router_frames.size() == 2u)
    {
        XS_CHECK(ByteSpanEqualsSpan(gm_router_frames[1], gm_outbound_payload));
    }
    if (gate_router_frames.size() == 2u)
    {
        XS_CHECK(ByteSpanEqualsSpan(gate_router_frames[1], gate_outbound_payload));
    }

    XS_CHECK(connector_messages["GM"].size() == 1u);
    XS_CHECK(connector_messages["Gate0"].size() == 1u);
    if (connector_messages["GM"].size() == 1u)
    {
        XS_CHECK(ByteSpanEqualsSpan(connector_messages["GM"].front(), gm_reply_payload));
    }
    if (connector_messages["Gate0"].size() == 1u)
    {
        XS_CHECK(ByteSpanEqualsSpan(connector_messages["Gate0"].front(), gate_reply_payload));
    }

    XS_CHECK(ByteSpanEqualsSpan(listener_received_payload, listener_payload));
    XS_CHECK(ByteSpanEqualsSpan(listener_reply, listener_reply_payload));

    XS_CHECK(!inner_network->IsRunning());
    XS_CHECK(inner_network->listener_state() == xs::ipc::ZmqListenerState::Stopped);
    XS_CHECK(inner_network->connection_state("GM") == xs::ipc::ZmqConnectionState::Stopped);
    XS_CHECK(inner_network->connection_state("Gate0") == xs::ipc::ZmqConnectionState::Stopped);
    XS_CHECK(inner_network->local_endpoint() == local_endpoint);
    XS_CHECK(configured_gm_remote_endpoint == gm_remote_endpoint);
    XS_CHECK(configured_gate_remote_endpoint == gate_remote_endpoint);
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Inner network listener started.") != std::string::npos);
    XS_CHECK(log_text.find("connectorId=GM") != std::string::npos);
    XS_CHECK(log_text.find("connectorId=Gate0") != std::string::npos);
    XS_CHECK(log_text.find("Inner network active connector received payload.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestInnerNetworkSupportsListenerAndMultipleConnectors();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " node inner network test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

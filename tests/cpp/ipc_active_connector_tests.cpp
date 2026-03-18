#include "ZmqActiveConnector.h"
#include "ZmqContext.h"

#include <asio/io_context.hpp>

#include <zmq.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
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

[[nodiscard]] std::vector<std::byte> BytesFromText(std::string_view text)
{
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (const char character : text)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return bytes;
}

[[nodiscard]] bool ByteSpanEqualsText(std::span<const std::byte> bytes, std::string_view text)
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

[[nodiscard]] bool ByteSpanEqualsSpan(std::span<const std::byte> left, std::span<const std::byte> right)
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

[[nodiscard]] bool SpinUntil(
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

[[nodiscard]] bool TryReceiveMultipartMessage(void* socket, std::vector<std::vector<std::byte>>* frames)
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

[[nodiscard]] bool SendRouterReply(
    void* router_socket,
    std::span<const std::byte> routing_id,
    std::span<const std::byte> payload)
{
    const void* routing_id_data = routing_id.empty() ? static_cast<const void*>("") : static_cast<const void*>(routing_id.data());
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

void TestConnectorRejectsInvalidOptionsAndSendBeforeStart()
{
    asio::io_context io_context;
    xs::ipc::ZmqContext context;

    XS_CHECK(context.IsValid());
    XS_CHECK(context.initialization_error().empty());

    xs::ipc::ZmqActiveConnector invalid_connector(
        io_context,
        context,
        {.remote_endpoint = "", .routing_id = "Gate0"});

    std::string error_message;
    XS_CHECK(invalid_connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::EndpointEmpty);
    XS_CHECK(error_message == std::string("ZeroMQ active connector remote_endpoint must not be empty."));
    XS_CHECK(invalid_connector.state() == xs::ipc::ZmqConnectionState::Stopped);

    xs::ipc::ZmqActiveConnector idle_connector(
        io_context,
        context,
        {.remote_endpoint = "tcp://127.0.0.1:6553", .routing_id = "Gate0"});

    const auto payload = BytesFromText("ping");
    error_message.clear();
    XS_CHECK(idle_connector.Send(payload, &error_message) == xs::ipc::ZmqSocketErrorCode::NotStarted);
    XS_CHECK(error_message == std::string("ZeroMQ active connector must be started before Send()."));
}

void TestConnectorConnectsAndExchangesMessages()
{
    RawZmqSocket router(ZMQ_ROUTER);
    XS_CHECK(router.IsValid());

    const std::string endpoint = router.BindLoopbackTcp();
    XS_CHECK(!endpoint.empty());

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<xs::ipc::ZmqConnectionState> states;
    std::vector<std::vector<std::byte>> received_messages;

    xs::ipc::ZmqActiveConnector connector(
        io_context,
        context,
        {
            .remote_endpoint = endpoint,
            .routing_id = "Gate0",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    connector.SetStateHandler([&states](xs::ipc::ZmqConnectionState state) {
        states.push_back(state);
    });
    connector.SetMessageHandler([&received_messages](std::vector<std::byte> message) {
        received_messages.push_back(std::move(message));
    });

    std::string error_message;
    XS_CHECK(connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    const bool connected = SpinUntil(io_context, std::chrono::seconds(2), [&connector]() {
        return connector.state() == xs::ipc::ZmqConnectionState::Connected;
    });
    XS_CHECK(connected);
    XS_CHECK(!states.empty());
    XS_CHECK(states.front() == xs::ipc::ZmqConnectionState::Connecting);
    XS_CHECK(std::find(states.begin(), states.end(), xs::ipc::ZmqConnectionState::Connected) != states.end());

    const auto outbound_payload = BytesFromText("register");
    XS_CHECK(connector.Send(outbound_payload, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    std::vector<std::vector<std::byte>> router_frames;
    const bool router_received = SpinUntil(io_context, std::chrono::seconds(2), [&]() {
        return TryReceiveMultipartMessage(router.socket(), &router_frames);
    });
    XS_CHECK(router_received);
    XS_CHECK(router_frames.size() == 2);
    XS_CHECK(ByteSpanEqualsText(router_frames[0], "Gate0"));
    XS_CHECK(ByteSpanEqualsSpan(router_frames[1], outbound_payload));

    const auto reply_payload = BytesFromText("accepted");
    XS_CHECK(SendRouterReply(router.socket(), router_frames[0], reply_payload));

    const bool client_received = SpinUntil(io_context, std::chrono::seconds(2), [&received_messages]() {
        return !received_messages.empty();
    });
    XS_CHECK(client_received);
    XS_CHECK(received_messages.size() == 1);
    XS_CHECK(ByteSpanEqualsSpan(received_messages.front(), reply_payload));

    connector.Stop();
    XS_CHECK(!connector.IsRunning());
    XS_CHECK(connector.state() == xs::ipc::ZmqConnectionState::Stopped);
    XS_CHECK(std::find(states.begin(), states.end(), xs::ipc::ZmqConnectionState::Stopped) != states.end());
}

void TestConnectorReportsRemoteDisconnect()
{
    auto router = std::make_unique<RawZmqSocket>(ZMQ_ROUTER);
    XS_CHECK(router->IsValid());

    const std::string endpoint = router->BindLoopbackTcp();
    XS_CHECK(!endpoint.empty());

    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<xs::ipc::ZmqConnectionState> states;

    xs::ipc::ZmqActiveConnector connector(
        io_context,
        context,
        {
            .remote_endpoint = endpoint,
            .routing_id = "Game0",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });
    connector.SetStateHandler([&states](xs::ipc::ZmqConnectionState state) {
        states.push_back(state);
    });

    std::string error_message;
    XS_CHECK(connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    const bool connected = SpinUntil(io_context, std::chrono::seconds(2), [&connector]() {
        return connector.state() == xs::ipc::ZmqConnectionState::Connected;
    });
    XS_CHECK(connected);

    router.reset();

    const bool disconnected = SpinUntil(io_context, std::chrono::seconds(2), [&states]() {
        return std::find(states.begin(), states.end(), xs::ipc::ZmqConnectionState::Disconnected) != states.end();
    });
    XS_CHECK(disconnected);
    XS_CHECK(connector.IsRunning());

    connector.Stop();
}

} // namespace

int main()
{
    TestConnectorRejectsInvalidOptionsAndSendBeforeStart();
    TestConnectorConnectsAndExchangesMessages();
    TestConnectorReportsRemoteDisconnect();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " ipc active connector test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

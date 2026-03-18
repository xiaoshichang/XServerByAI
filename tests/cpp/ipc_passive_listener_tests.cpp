#include "ZmqActiveConnector.h"
#include "ZmqContext.h"
#include "ZmqPassiveListener.h"

#include <asio/io_context.hpp>

#include <zmq.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
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

struct RoutedMessage final
{
    std::vector<std::byte> routing_id;
    std::vector<std::byte> payload;
};

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

    [[nodiscard]] bool SetRoutingId(std::string_view routing_id)
    {
        if (socket_ == nullptr)
        {
            return false;
        }

        return zmq_setsockopt(socket_, ZMQ_ROUTING_ID, routing_id.data(), routing_id.size()) == 0;
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

    [[nodiscard]] bool SendMultipart(std::span<const std::byte> first, std::span<const std::byte> second)
    {
        if (socket_ == nullptr)
        {
            return false;
        }

        const void* first_data = first.empty() ? static_cast<const void*>("") : static_cast<const void*>(first.data());
        if (zmq_send(socket_, first_data, first.size(), ZMQ_SNDMORE) < 0)
        {
            return false;
        }

        const void* second_data = second.empty() ? static_cast<const void*>("") : static_cast<const void*>(second.data());
        return zmq_send(socket_, second_data, second.size(), 0) >= 0;
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

void TestListenerRejectsInvalidOptionsAndSendBeforeStart()
{
    asio::io_context io_context;
    xs::ipc::ZmqContext context;

    XS_CHECK(context.IsValid());
    XS_CHECK(context.initialization_error().empty());

    xs::ipc::ZmqPassiveListener invalid_listener(
        io_context,
        context,
        {.local_endpoint = ""});

    std::string error_message;
    XS_CHECK(invalid_listener.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::EndpointEmpty);
    XS_CHECK(error_message == std::string("ZeroMQ passive listener local_endpoint must not be empty."));
    XS_CHECK(invalid_listener.state() == xs::ipc::ZmqListenerState::Stopped);

    xs::ipc::ZmqPassiveListener idle_listener(
        io_context,
        context,
        {.local_endpoint = "tcp://127.0.0.1:*"});

    const auto routing_id = BytesFromText("Gate0");
    const auto payload = BytesFromText("ping");
    error_message.clear();
    XS_CHECK(idle_listener.Send(routing_id, payload, &error_message) == xs::ipc::ZmqSocketErrorCode::NotStarted);
    XS_CHECK(error_message == std::string("ZeroMQ passive listener must be started before Send()."));
}

void TestListenerBindsAndExchangesMessages()
{
    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<xs::ipc::ZmqListenerState> listener_states;
    std::vector<RoutedMessage> routed_messages;

    xs::ipc::ZmqPassiveListener listener(
        io_context,
        context,
        {
            .local_endpoint = "tcp://127.0.0.1:*",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .handshake_interval_ms = 1000,
        });
    listener.SetStateHandler([&listener_states](xs::ipc::ZmqListenerState state) {
        listener_states.push_back(state);
    });
    listener.SetMessageHandler([&routed_messages](std::vector<std::byte> routing_id, std::vector<std::byte> payload) {
        routed_messages.push_back({std::move(routing_id), std::move(payload)});
    });

    std::string error_message;
    XS_CHECK(listener.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(listener.IsRunning());
    XS_CHECK(listener.state() == xs::ipc::ZmqListenerState::Listening);
    XS_CHECK(!listener.bound_endpoint().empty());
    XS_CHECK(!listener_states.empty());
    XS_CHECK(listener_states.front() == xs::ipc::ZmqListenerState::Listening);

    xs::ipc::ZmqActiveConnector connector(
        io_context,
        context,
        {
            .remote_endpoint = std::string(listener.bound_endpoint()),
            .routing_id = "Gate0",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .reconnect_interval_ms = 25,
            .reconnect_interval_max_ms = 50,
            .handshake_interval_ms = 1000,
        });

    std::vector<std::vector<std::byte>> connector_messages;
    connector.SetMessageHandler([&connector_messages](std::vector<std::byte> payload) {
        connector_messages.push_back(std::move(payload));
    });

    XS_CHECK(connector.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    const bool connected = SpinUntil(io_context, std::chrono::seconds(2), [&connector]() {
        return connector.state() == xs::ipc::ZmqConnectionState::Connected;
    });
    XS_CHECK(connected);

    const auto empty_routing_id = std::vector<std::byte>{};
    const auto empty_routing_payload = BytesFromText("probe");
    error_message.clear();
    XS_CHECK(
        listener.Send(empty_routing_id, empty_routing_payload, &error_message) == xs::ipc::ZmqSocketErrorCode::EmptyRoutingId);
    XS_CHECK(error_message == std::string("ZeroMQ passive listener routing_id must not be empty."));

    const auto outbound_payload = BytesFromText("register");
    error_message.clear();
    XS_CHECK(connector.Send(outbound_payload, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    const bool listener_received = SpinUntil(io_context, std::chrono::seconds(2), [&routed_messages]() {
        return !routed_messages.empty();
    });
    XS_CHECK(listener_received);
    XS_CHECK(routed_messages.size() == 1);
    XS_CHECK(ByteSpanEqualsText(routed_messages.front().routing_id, "Gate0"));
    XS_CHECK(ByteSpanEqualsSpan(routed_messages.front().payload, outbound_payload));

    const auto reply_payload = BytesFromText("accepted");
    error_message.clear();
    XS_CHECK(listener.Send(routed_messages.front().routing_id, reply_payload, &error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());

    const bool connector_received = SpinUntil(io_context, std::chrono::seconds(2), [&connector_messages]() {
        return !connector_messages.empty();
    });
    XS_CHECK(connector_received);
    XS_CHECK(connector_messages.size() == 1);
    XS_CHECK(ByteSpanEqualsSpan(connector_messages.front(), reply_payload));

    connector.Stop();
    listener.Stop();
    XS_CHECK(!listener.IsRunning());
    XS_CHECK(listener.state() == xs::ipc::ZmqListenerState::Stopped);
    XS_CHECK(listener.bound_endpoint().empty());
    XS_CHECK(std::find(listener_states.begin(), listener_states.end(), xs::ipc::ZmqListenerState::Stopped) != listener_states.end());
}

void TestListenerRejectsMultipartPayloadMessages()
{
    asio::io_context io_context;
    xs::ipc::ZmqContext context;
    XS_CHECK(context.IsValid());

    std::vector<RoutedMessage> routed_messages;

    xs::ipc::ZmqPassiveListener listener(
        io_context,
        context,
        {
            .local_endpoint = "tcp://127.0.0.1:*",
            .poll_interval = std::chrono::milliseconds(2),
            .send_high_water_mark = 16,
            .receive_high_water_mark = 16,
            .handshake_interval_ms = 1000,
        });
    listener.SetMessageHandler([&routed_messages](std::vector<std::byte> routing_id, std::vector<std::byte> payload) {
        routed_messages.push_back({std::move(routing_id), std::move(payload)});
    });

    std::string error_message;
    XS_CHECK(listener.Start(&error_message) == xs::ipc::ZmqSocketErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(!listener.bound_endpoint().empty());

    RawZmqSocket dealer(ZMQ_DEALER);
    XS_CHECK(dealer.IsValid());
    XS_CHECK(dealer.SetRoutingId("DealerMultipart"));
    XS_CHECK(dealer.Connect(listener.bound_endpoint()));

    const auto first_part = BytesFromText("part-a");
    const auto second_part = BytesFromText("part-b");
    XS_CHECK(dealer.SendMultipart(first_part, second_part));

    const bool multipart_rejected = SpinUntil(io_context, std::chrono::seconds(2), [&listener]() {
        return listener.last_error_message() ==
               std::string_view("ZeroMQ passive listener only supports routing id plus single-frame payload.");
    });
    XS_CHECK(multipart_rejected);
    XS_CHECK(routed_messages.empty());

    const auto valid_payload = BytesFromText("hello");
    XS_CHECK(dealer.Send(valid_payload));

    const bool valid_message_received = SpinUntil(io_context, std::chrono::seconds(2), [&routed_messages]() {
        return !routed_messages.empty();
    });
    XS_CHECK(valid_message_received);
    XS_CHECK(routed_messages.size() == 1);
    XS_CHECK(ByteSpanEqualsText(routed_messages.front().routing_id, "DealerMultipart"));
    XS_CHECK(ByteSpanEqualsSpan(routed_messages.front().payload, valid_payload));

    listener.Stop();
}

} // namespace

int main()
{
    TestListenerRejectsInvalidOptionsAndSendBeforeStart();
    TestListenerBindsAndExchangesMessages();
    TestListenerRejectsMultipartPayloadMessages();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " ipc passive listener test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include "InnerNetwork.h"
#include "Logging.h"
#include "MainEventLoop.h"

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

void TestActiveInnerNetworkConnectsReceivesAndReportsDisconnect()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-inner-network-active");
    const std::filesystem::path log_dir = base_path / "logs";

    RawZmqSocket router(ZMQ_ROUTER);
    XS_CHECK(router.IsValid());

    const std::string remote_endpoint = router.BindLoopbackTcp();
    XS_CHECK(!remote_endpoint.empty());

    xs::core::Logger logger(MakeLoggerOptions(log_dir, xs::core::ProcessType::Gate, "Gate0"));
    xs::core::MainEventLoop event_loop({.thread_name = "node-inner-network-active"});

    xs::node::InnerNetworkOptions options;
    options.mode = xs::node::InnerNetworkMode::ActiveConnector;
    options.local_endpoint = "tcp://127.0.0.1:7000";
    options.remote_endpoint = remote_endpoint;

    auto inner_network = std::make_shared<xs::node::InnerNetwork>(event_loop, logger, std::move(options));
    const std::vector<std::byte> outbound_payload = BytesFromText("gate-bootstrap");
    const std::vector<std::byte> reply_payload = BytesFromText("gm-ready");

    std::vector<std::vector<std::byte>> received_messages;
    std::vector<std::vector<std::byte>> router_frames;
    bool seen_connected = false;
    bool sent_outbound_payload = false;
    bool router_replied = false;
    bool router_closed = false;
    bool seen_disconnected = false;

    inner_network->SetMessageHandler([&received_messages](std::vector<std::byte> routing_id, std::vector<std::byte> payload) {
        XS_CHECK(routing_id.empty());
        received_messages.push_back(std::move(payload));
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

        const xs::core::TimerCreateResult poll_timer =
            running_loop.timers().CreateRepeating(
                std::chrono::milliseconds(5),
                [&running_loop, &router, &router_frames, &seen_connected, &sent_outbound_payload, &router_replied, &router_closed, &seen_disconnected, &received_messages, &outbound_payload, &reply_payload, inner_network]() {
                    if (!seen_connected &&
                        inner_network->connection_state() == xs::ipc::ZmqConnectionState::Connected)
                    {
                        seen_connected = true;
                    }

                    if (seen_connected && !sent_outbound_payload)
                    {
                        const xs::node::NodeErrorCode send_result = inner_network->Send({}, outbound_payload);
                        XS_CHECK_MSG(
                            send_result == xs::node::NodeErrorCode::None,
                            inner_network->last_error_message().data());
                        sent_outbound_payload = send_result == xs::node::NodeErrorCode::None;
                    }

                    if (sent_outbound_payload && !router_replied && TryReceiveMultipartMessage(router.socket(), &router_frames))
                    {
                        XS_CHECK(router_frames.size() == 2u);
                        if (router_frames.size() == 2u)
                        {
                            XS_CHECK(SendRouterReply(router.socket(), router_frames[0], reply_payload));
                        }

                        router_replied = true;
                    }

                    if (router_replied && !router_closed && !received_messages.empty())
                    {
                        router.Close();
                        router_closed = true;
                    }

                    if (router_closed &&
                        !seen_disconnected &&
                        inner_network->connection_state() != xs::ipc::ZmqConnectionState::Connected &&
                        inner_network->connection_state() != xs::ipc::ZmqConnectionState::Stopped)
                    {
                        seen_disconnected = true;
                    }

                    if (router_closed && !received_messages.empty() && seen_disconnected)
                    {
                        running_loop.RequestStop();
                    }
                });
        if (!xs::core::IsTimerID(poll_timer))
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to create active inner network poll timer.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        const xs::core::TimerCreateResult timeout_timer =
            running_loop.timers().CreateOnce(std::chrono::seconds(2), [&running_loop]() {
                running_loop.RequestStop();
            });
        if (!xs::core::IsTimerID(timeout_timer))
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to create active inner network timeout timer.";
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

    XS_CHECK(seen_connected);
    XS_CHECK(sent_outbound_payload);
    XS_CHECK(router_replied);
    XS_CHECK(seen_disconnected);
    XS_CHECK(router_frames.size() == 2u);
    if (router_frames.size() == 2u)
    {
        XS_CHECK(ByteSpanEqualsSpan(router_frames[1], outbound_payload));
    }

    XS_CHECK(received_messages.size() == 1u);
    if (received_messages.size() == 1u)
    {
        XS_CHECK(ByteSpanEqualsSpan(received_messages.front(), reply_payload));
    }

    XS_CHECK(!inner_network->IsRunning());
    XS_CHECK(inner_network->connection_state() == xs::ipc::ZmqConnectionState::Stopped);
    XS_CHECK(inner_network->local_endpoint() == "tcp://127.0.0.1:7000");
    XS_CHECK(inner_network->remote_endpoint() == remote_endpoint);
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Inner network active connector started.") != std::string::npos);
    XS_CHECK(log_text.find("Inner network active connector state changed.") != std::string::npos);
    XS_CHECK(log_text.find("Inner network active connector received payload.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestActiveInnerNetworkConnectsReceivesAndReportsDisconnect();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " node inner network test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

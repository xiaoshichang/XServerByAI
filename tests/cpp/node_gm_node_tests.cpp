#include "GmNode.h"
#include "InnerNetwork.h"
#include "Json.h"
#include "NodeCommon.h"
#include "ZmqActiveConnector.h"
#include "ZmqContext.h"

#include <asio/error.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
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

xs::core::Json MakeClusterConfigJson(
    const std::filesystem::path& base_path,
    bool include_gm_inner_endpoint,
    bool include_gm_control_endpoint,
    std::uint16_t gm_inner_port = 5000u,
    std::uint16_t gm_control_port = 5100u)
{
    const std::string root_log_dir = (base_path / "logs").string();

    xs::core::Json gm_block;
    if (include_gm_inner_endpoint)
    {
        gm_block = xs::core::Json{
            {"innerNetwork",
             {{"listenEndpoint", {{"host", "127.0.0.1"}, {"port", gm_inner_port}}}}},
        };
    }
    else
    {
        gm_block = xs::core::Json::object();
    }

    if (include_gm_control_endpoint)
    {
        gm_block["controlNetwork"] = xs::core::Json{
            {"listenEndpoint", {{"host", "127.0.0.1"}, {"port", gm_control_port}}},
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
        {"gm", gm_block},
        {"gate",
         xs::core::Json{
             {"Gate0",
              xs::core::Json{
                  {"innerNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "0.0.0.0"}, {"port", 7000}}},
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
    bool include_gm_inner_endpoint,
    bool include_gm_control_endpoint,
    std::uint16_t gm_control_port,
    std::filesystem::path* file_path)
{
    if (file_path == nullptr)
    {
        XS_CHECK(false);
        return false;
    }

    *file_path = base_path / "config.json";
    return WriteJsonFile(
        *file_path,
        MakeClusterConfigJson(base_path, include_gm_inner_endpoint, include_gm_control_endpoint, 5000u, gm_control_port));
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

std::vector<std::byte> BytesFromString(std::string_view value)
{
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());

    for (const char ch : value)
    {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }

    return bytes;
}

struct HttpResponse
{
    int status_code{0};
    std::string content_type{};
    std::string body{};
};

bool TryParseHttpResponse(std::string_view response_text, HttpResponse* output, std::string* error_message)
{
    if (output == nullptr)
    {
        XS_CHECK(false);
        return false;
    }

    const std::size_t header_end = response_text.find("\r\n\r\n");
    if (header_end == std::string_view::npos)
    {
        if (error_message != nullptr)
        {
            *error_message = "HTTP response is missing the header terminator.";
        }
        return false;
    }

    const std::size_t status_line_end = response_text.find("\r\n");
    if (status_line_end == std::string_view::npos || status_line_end > header_end)
    {
        if (error_message != nullptr)
        {
            *error_message = "HTTP response is missing a valid status line.";
        }
        return false;
    }

    const std::string status_line(response_text.substr(0, status_line_end));
    std::size_t first_space = status_line.find(' ');
    if (first_space == std::string::npos)
    {
        if (error_message != nullptr)
        {
            *error_message = "HTTP response status line is invalid.";
        }
        return false;
    }

    std::size_t second_space = status_line.find(' ', first_space + 1u);
    const std::string code_text = second_space == std::string::npos
                                      ? status_line.substr(first_space + 1u)
                                      : status_line.substr(first_space + 1u, second_space - first_space - 1u);
    output->status_code = std::atoi(code_text.c_str());

    output->content_type.clear();
    std::size_t line_start = status_line_end + 2u;
    while (line_start < header_end)
    {
        const std::size_t line_end = response_text.find("\r\n", line_start);
        const std::size_t line_length = (line_end == std::string_view::npos ? header_end : line_end) - line_start;
        const std::string line(response_text.substr(line_start, line_length));
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            const std::string name = line.substr(0, colon);
            std::string value = line.substr(colon + 1u);
            while (!value.empty() && value.front() == ' ')
            {
                value.erase(value.begin());
            }

            if (name == "Content-Type")
            {
                output->content_type = std::move(value);
            }
        }

        if (line_end == std::string_view::npos || line_end >= header_end)
        {
            break;
        }
        line_start = line_end + 2u;
    }

    output->body = std::string(response_text.substr(header_end + 4u));
    if (error_message != nullptr)
    {
        error_message->clear();
    }
    return true;
}

bool TrySendHttpRequest(
    std::uint16_t port,
    std::string_view request_text,
    HttpResponse* response,
    std::string* error_message)
{
    if (response == nullptr)
    {
        XS_CHECK(false);
        return false;
    }

    asio::io_context io_context;
    asio::ip::tcp::socket socket(io_context);

    std::error_code error_code;
    socket.connect(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port), error_code);
    if (error_code)
    {
        if (error_message != nullptr)
        {
            *error_message = error_code.message();
        }
        return false;
    }

    const std::size_t bytes_written = asio::write(socket, asio::buffer(request_text.data(), request_text.size()), error_code);
    if (error_code || bytes_written != request_text.size())
    {
        if (error_message != nullptr)
        {
            *error_message = error_code ? error_code.message() : "HTTP request write was incomplete.";
        }
        return false;
    }

    socket.shutdown(asio::ip::tcp::socket::shutdown_send, error_code);
    error_code.clear();

    std::string response_text;
    std::array<char, 2048> buffer{};
    for (;;)
    {
        const std::size_t bytes_read = socket.read_some(asio::buffer(buffer), error_code);
        if (error_code == asio::error::eof)
        {
            break;
        }

        if (error_code)
        {
            if (error_message != nullptr)
            {
                *error_message = error_code.message();
            }
            return false;
        }

        response_text.append(buffer.data(), bytes_read);
    }

    return TryParseHttpResponse(response_text, response, error_message);
}

bool WaitForHttpReady(std::uint16_t port, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const std::string request = "GET /healthz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    while (std::chrono::steady_clock::now() < deadline)
    {
        HttpResponse response;
        std::string error_message;
        if (TrySendHttpRequest(port, request, &response, &error_message) && response.status_code == 200)
        {
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return false;
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

    void Join()
    {
        if (run_thread_.joinable())
        {
            run_thread_.join();
        }
    }

    [[nodiscard]] xs::node::NodeErrorCode run_result() const noexcept
    {
        return run_result_;
    }

    [[nodiscard]] std::string_view run_error() const noexcept
    {
        return run_error_;
    }

    bool Uninit()
    {
        const xs::node::NodeErrorCode uninit_result = node_.Uninit();
        XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node_.last_error_message().data());
        return uninit_result == xs::node::NodeErrorCode::None;
    }

  private:
    xs::node::GmNode node_;
    std::thread run_thread_{};
    xs::node::NodeErrorCode run_result_{xs::node::NodeErrorCode::None};
    std::string run_error_{};
};

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

std::string ResolveNodeError(xs::node::NodeErrorCode code, std::string_view error_message)
{
    if (!error_message.empty())
    {
        return std::string(error_message);
    }

    return std::string(xs::node::NodeErrorMessage(code));
}

void TestGmNodeRejectsMissingInnerNetworkEndpointConfig()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-node-missing-inner");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, false, true, 5100u, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::GmNode node({
        .config_path = config_path,
        .node_id = "GM",
    });

    const xs::node::NodeErrorCode result = node.Init();

    XS_CHECK(result == xs::node::NodeErrorCode::ConfigLoadFailed);
    XS_CHECK_MSG(
        std::string(node.last_error_message()).find("innerNetwork") != std::string::npos,
        node.last_error_message().data());

    CleanupTestDirectory(base_path);
}

void TestGmNodeRejectsNonGmSelector()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-node-non-gm");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, true, true, 5100u, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::GmNode node({
        .config_path = config_path,
        .node_id = "Gate0",
    });

    const xs::node::NodeErrorCode result = node.Init();

    XS_CHECK(result == xs::node::NodeErrorCode::InvalidArgument);
    XS_CHECK_MSG(
        std::string(node.last_error_message()).find("GM node requires nodeId resolving to GM.") !=
            std::string::npos,
        node.last_error_message().data());

    CleanupTestDirectory(base_path);
}

void TestGmNodeRejectsMissingControlNetworkEndpointConfig()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-node-missing-control");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, true, false, 5100u, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::GmNode node({
        .config_path = config_path,
        .node_id = "GM",
    });

    const xs::node::NodeErrorCode result = node.Init();

    XS_CHECK(result == xs::node::NodeErrorCode::ConfigLoadFailed);
    XS_CHECK_MSG(
        std::string(node.last_error_message()).find("controlNetwork") != std::string::npos,
        node.last_error_message().data());

    CleanupTestDirectory(base_path);
}

void TestGmNodeServesHealthStatusAndShutdownOverHttp()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-http-control");
    const std::uint16_t inner_port = AcquireLoopbackPort();
    const std::uint16_t control_port = AcquireLoopbackPort();
    const std::filesystem::path config_path = base_path / "config.json";
    if (!WriteJsonFile(config_path, MakeClusterConfigJson(base_path, true, true, inner_port, control_port)))
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

    XS_CHECK(WaitForHttpReady(control_port, std::chrono::seconds(2)));

    HttpResponse health_response;
    std::string error_message;
    XS_CHECK_MSG(
        TrySendHttpRequest(
            control_port,
            "GET /healthz HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            &health_response,
            &error_message),
        error_message.c_str());
    XS_CHECK(health_response.status_code == 200);
    XS_CHECK(health_response.content_type.find("application/json") != std::string::npos);
    XS_CHECK(health_response.body.find("\"status\":\"ok\"") != std::string::npos);
    XS_CHECK(health_response.body.find("\"nodeId\":\"GM\"") != std::string::npos);

    HttpResponse status_response;
    error_message.clear();
    XS_CHECK_MSG(
        TrySendHttpRequest(
            control_port,
            "GET /status HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            &status_response,
            &error_message),
        error_message.c_str());
    XS_CHECK(status_response.status_code == 200);
    XS_CHECK(status_response.body.find("\"registeredProcessCount\":0") != std::string::npos);
    XS_CHECK(status_response.body.find("\"running\":true") != std::string::npos);
    XS_CHECK(status_response.body.find("tcp://127.0.0.1:" + std::to_string(inner_port)) != std::string::npos);
    XS_CHECK(status_response.body.find("127.0.0.1:" + std::to_string(control_port)) != std::string::npos);

    HttpResponse shutdown_response;
    error_message.clear();
    XS_CHECK_MSG(
        TrySendHttpRequest(
            control_port,
            "POST /shutdown HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
            &shutdown_response,
            &error_message),
        error_message.c_str());
    XS_CHECK(shutdown_response.status_code == 200);
    XS_CHECK(shutdown_response.body.find("\"status\":\"stopping\"") != std::string::npos);

    gm_node.Join();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());
    XS_CHECK(gm_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));
    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("GM control HTTP listener started.") != std::string::npos);
    XS_CHECK(log_text.find("GM control HTTP request handled.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

void TestInnerNetworkWildcardBindAndReceivesPayload()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-inner-network-listener");
    const std::filesystem::path log_dir = base_path / "logs" / "gm";

    xs::core::Logger logger(MakeLoggerOptions(log_dir, xs::core::ProcessType::Gm, "GM"));
    xs::core::MainEventLoop event_loop({.thread_name = "inner-network-listener"});

    xs::node::InnerNetworkOptions options;
    options.mode = xs::node::InnerNetworkMode::PassiveListener;
    options.local_endpoint = "tcp://127.0.0.1:*";

    auto inner_network = std::make_shared<xs::node::InnerNetwork>(event_loop, logger, std::move(options));
    auto payload = std::make_shared<std::vector<std::byte>>(BytesFromString("gm-test-payload"));

    std::shared_ptr<xs::ipc::ZmqContext> connector_context;
    std::shared_ptr<xs::ipc::ZmqActiveConnector> connector;
    std::vector<xs::ipc::ZmqConnectionState> connector_states;
    xs::ipc::ZmqListenerMetricsSnapshot received_snapshot;
    bool send_scheduled = false;
    bool send_attempted = false;
    bool send_succeeded = false;
    bool received_message = false;
    std::string send_error;
    std::string bound_endpoint;

    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [&](xs::core::MainEventLoop& running_loop, std::string* error_message) {
        const xs::node::NodeErrorCode init_result = inner_network->Init();
        if (init_result != xs::node::NodeErrorCode::None)
        {
            if (error_message != nullptr)
            {
                *error_message = ResolveNodeError(init_result, inner_network->last_error_message());
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        const xs::node::NodeErrorCode run_result = inner_network->Run();
        if (run_result != xs::node::NodeErrorCode::None)
        {
            if (error_message != nullptr)
            {
                *error_message = ResolveNodeError(run_result, inner_network->last_error_message());
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        bound_endpoint = std::string(inner_network->bound_endpoint());

        connector_context = std::make_shared<xs::ipc::ZmqContext>();
        if (!connector_context->IsValid())
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to initialize test ZeroMQ context: " +
                                 std::string(connector_context->initialization_error());
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        xs::ipc::ZmqActiveConnectorOptions connector_options;
        connector_options.remote_endpoint = bound_endpoint;
        connector_options.routing_id = "gm-test-client";

        connector = std::make_shared<xs::ipc::ZmqActiveConnector>(
            running_loop.context(),
            *connector_context,
            std::move(connector_options));
        connector->SetStateHandler([&, payload](xs::ipc::ZmqConnectionState state) {
            connector_states.push_back(state);
            if (state != xs::ipc::ZmqConnectionState::Connected || send_scheduled)
            {
                return;
            }

            send_scheduled = true;
            const xs::core::TimerCreateResult send_timer =
                running_loop.timers().CreateOnce(std::chrono::milliseconds(10), [&, payload]() {
                    send_attempted = true;
                    std::string local_error;
                    send_succeeded = connector->Send(*payload, &local_error) == xs::ipc::ZmqSocketErrorCode::None;
                    send_error = std::move(local_error);
                });
            XS_CHECK(xs::core::IsTimerID(send_timer));
        });

        std::string connector_error;
        if (connector->Start(&connector_error) != xs::ipc::ZmqSocketErrorCode::None)
        {
            if (error_message != nullptr)
            {
                *error_message = connector_error;
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        const xs::core::TimerCreateResult poll_timer =
            running_loop.timers().CreateRepeating(std::chrono::milliseconds(5), [&]() {
                const xs::ipc::ZmqListenerMetricsSnapshot snapshot = inner_network->metrics();
                if (snapshot.received_message_count == 0U)
                {
                    return;
                }

                received_message = true;
                received_snapshot = snapshot;
                running_loop.RequestStop();
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
            running_loop.timers().CreateOnce(std::chrono::milliseconds(1500), [&]() {
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
        if (connector != nullptr)
        {
            connector->Stop();
        }

        (void)inner_network->Uninit();
    };

    std::string error_message;
    const xs::core::MainEventLoopErrorCode run_result = event_loop.Run(std::move(hooks), &error_message);
    XS_CHECK_MSG(run_result == xs::core::MainEventLoopErrorCode::None, error_message.c_str());

    logger.Flush();

    XS_CHECK(!bound_endpoint.empty());
    XS_CHECK(bound_endpoint.find('*') == std::string::npos);
    XS_CHECK(send_scheduled);
    XS_CHECK(send_attempted);
    XS_CHECK_MSG(send_succeeded, send_error.c_str());
    XS_CHECK(received_message);
    XS_CHECK(received_snapshot.received_message_count >= 1U);
    XS_CHECK(received_snapshot.active_connection_count >= 1U);
    XS_CHECK(!inner_network->IsRunning());
    XS_CHECK(inner_network->listener_state() == xs::ipc::ZmqListenerState::Stopped);
    XS_CHECK(std::find(connector_states.begin(), connector_states.end(), xs::ipc::ZmqConnectionState::Connected) !=
             connector_states.end());
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Inner network listener started.") != std::string::npos);
    XS_CHECK(log_text.find("Inner network listener received payload.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestGmNodeRejectsMissingInnerNetworkEndpointConfig();
    TestGmNodeRejectsNonGmSelector();
    TestGmNodeRejectsMissingControlNetworkEndpointConfig();
    TestGmNodeServesHealthStatusAndShutdownOverHttp();
    TestInnerNetworkWildcardBindAndReceivesPayload();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " node gm node test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}


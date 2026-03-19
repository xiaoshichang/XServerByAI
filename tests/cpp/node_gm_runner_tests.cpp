#include "Json.h"
#include "NodeGmRunner.h"
#include "NodeRuntime.h"
#include "ZmqActiveConnector.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

xs::core::Json MakeValidClusterConfigJson(const std::filesystem::path& base_path)
{
    const std::string root_log_dir = (base_path / "logs" / "root").string();
    const std::string gate_log_dir = (base_path / "logs" / "gate").string();
    const std::string game_log_dir = (base_path / "logs" / "game").string();

    return xs::core::Json{
        {"serverGroup", {{"id", "local-dev"}, {"environment", "dev"}}},
        {"logging",
         {{"rootDir", root_log_dir},
          {"minLevel", "Info"},
          {"flushIntervalMs", 1000},
          {"rotateDaily", true},
          {"maxFileSizeMB", 64},
          {"maxRetainedFiles", 10}}},
        {"gm",
         {{"control",
           {{"listenEndpoint", {{"host", "127.0.0.1"}, {"port", 5000}}}}}}},
        {"gate",
         {{"gate0",
           {{"nodeId", "Gate0"},
            {"service", {{"listenEndpoint", {{"host", "0.0.0.0"}, {"port", 7000}}}}},
            {"kcp", {{"sndwnd", 256}}},
            {"logging", {{"minLevel", "Debug"}, {"rootDir", gate_log_dir}}}}}}},
        {"game",
         {{"game0",
           {{"nodeId", "Game0"},
            {"service", {{"listenEndpoint", {{"host", "127.0.0.1"}, {"port", 7100}}}}},
            {"managed", {{"assemblyName", "XServer.Managed.GameLogic"}}},
            {"logging", {{"rootDir", game_log_dir}}}}}}},
    };
}

bool WriteRuntimeConfig(const std::filesystem::path& base_path, std::filesystem::path* file_path)
{
    if (file_path == nullptr)
    {
        XS_CHECK(false);
        return false;
    }

    *file_path = base_path / "config.json";
    return WriteJsonFile(*file_path, MakeValidClusterConfigJson(base_path));
}

bool LoadRuntimeContext(
    const std::filesystem::path& config_path,
    std::string_view selector,
    xs::node::NodeRuntimeContext* output)
{
    xs::node::NodeCommandLineArgs args;
    args.config_path = config_path;
    args.selector = std::string(selector);

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result = xs::node::LoadNodeRuntimeContext(args, output, &error_message);
    XS_CHECK_MSG(result == xs::node::NodeRuntimeErrorCode::None, error_message.c_str());
    return result == xs::node::NodeRuntimeErrorCode::None;
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

void TestRunGmNodeRejectsMissingControlEndpoint()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-runner-missing-control");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::NodeRuntimeContext context;
    if (!LoadRuntimeContext(config_path, "gm", &context))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    context.node_config.control_listen_endpoint.reset();

    xs::core::Logger logger(MakeLoggerOptions(base_path / "logs" / "root", xs::core::ProcessType::Gm, "GM"));
    xs::core::MainEventLoop event_loop({.thread_name = "gm-runner-no-control"});
    xs::node::NodeRoleRuntimeBindings runtime_bindings;

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunGmNode(context, logger, event_loop, &runtime_bindings, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::InvalidArgument);
    XS_CHECK_MSG(error_message.find("control.listenEndpoint") != std::string::npos, error_message.c_str());
    XS_CHECK(!runtime_bindings.on_stop);

    CleanupTestDirectory(base_path);
}

void TestRunGmNodeRejectsNonGmProcessType()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-runner-non-gm");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::NodeRuntimeContext context;
    if (!LoadRuntimeContext(config_path, "gate0", &context))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::Logger logger(MakeLoggerOptions(base_path / "logs" / "gate", xs::core::ProcessType::Gate, "Gate0"));
    xs::core::MainEventLoop event_loop({.thread_name = "gm-runner-wrong-type"});
    xs::node::NodeRoleRuntimeBindings runtime_bindings;

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunGmNode(context, logger, event_loop, &runtime_bindings, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::InvalidArgument);
    XS_CHECK_MSG(error_message.find("process_type = GM") != std::string::npos, error_message.c_str());
    XS_CHECK(!runtime_bindings.on_stop);

    CleanupTestDirectory(base_path);
}

void TestGmControlListenerWildcardBindAndReceivesPayload()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-gm-control-listener");
    const std::filesystem::path log_dir = base_path / "logs" / "gm";

    xs::core::Logger logger(MakeLoggerOptions(log_dir, xs::core::ProcessType::Gm, "GM"));
    xs::core::MainEventLoop event_loop({.thread_name = "gm-control-listener"});

    auto listener = std::make_shared<xs::node::GmControlListener>(event_loop, logger, "tcp://127.0.0.1:*");
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
        const xs::node::NodeRuntimeErrorCode listener_result = listener->Start(error_message);
        if (listener_result != xs::node::NodeRuntimeErrorCode::None)
        {
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        bound_endpoint = std::string(listener->bound_endpoint());

        connector_context = std::make_shared<xs::ipc::ZmqContext>();
        if (!connector_context->IsValid())
        {
            if (error_message != nullptr)
            {
                *error_message =
                    "Failed to initialize GM runner test ZeroMQ context: " + std::string(connector_context->initialization_error());
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        xs::ipc::ZmqActiveConnectorOptions connector_options;
        connector_options.remote_endpoint = bound_endpoint;
        connector_options.routing_id = "gm-test-client";

        connector = std::make_shared<xs::ipc::ZmqActiveConnector>(running_loop.context(), *connector_context, std::move(connector_options));
        connector->SetStateHandler([&](xs::ipc::ZmqConnectionState state) {
            connector_states.push_back(state);
            if (state != xs::ipc::ZmqConnectionState::Connected || send_scheduled)
            {
                return;
            }

            send_scheduled = true;
            const xs::core::TimerCreateResult send_timer = running_loop.timers().CreateOnce(std::chrono::milliseconds(10), [&, payload] {
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

        const xs::core::TimerCreateResult poll_timer = running_loop.timers().CreateRepeating(std::chrono::milliseconds(5), [&]() {
            const xs::ipc::ZmqListenerMetricsSnapshot snapshot = listener->metrics();
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
                *error_message = "Failed to create GM runner test poll timer.";
            }
            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        const xs::core::TimerCreateResult timeout_timer = running_loop.timers().CreateOnce(std::chrono::milliseconds(1500), [&]() {
            running_loop.RequestStop();
        });
        if (!xs::core::IsTimerID(timeout_timer))
        {
            if (error_message != nullptr)
            {
                *error_message = "Failed to create GM runner test timeout timer.";
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

        listener->Stop();
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
    XS_CHECK(!listener->IsRunning());
    XS_CHECK(listener->listener_state() == xs::ipc::ZmqListenerState::Stopped);
    XS_CHECK(std::find(connector_states.begin(), connector_states.end(), xs::ipc::ZmqConnectionState::Connected) !=
             connector_states.end());
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("GM control listener started.") != std::string::npos);
    XS_CHECK(log_text.find("GM control listener received control-plane payload.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestRunGmNodeRejectsMissingControlEndpoint();
    TestRunGmNodeRejectsNonGmProcessType();
    TestGmControlListenerWildcardBindAndReceivesPayload();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " node gm runner test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

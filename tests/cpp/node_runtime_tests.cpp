#include "Json.h"
#include "NodeRuntime.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

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

void TestParseNodeCommandLineSuccess()
{
    char program[] = "xserver-node";
    char config_path[] = "configs/local-dev.json";
    char selector[] = "gate0";
    char* argv[] = {program, config_path, selector};

    xs::node::NodeCommandLineArgs args;
    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result = xs::node::ParseNodeCommandLine(3, argv, &args, &error_message);

    XS_CHECK_MSG(result == xs::node::NodeRuntimeErrorCode::None, error_message.c_str());
    XS_CHECK(error_message.empty());
    XS_CHECK(args.config_path == std::filesystem::path(config_path));
    XS_CHECK(args.selector == "gate0");
}

void TestParseNodeCommandLineRejectsInvalidArgumentCount()
{
    char program[] = "xserver-node";
    char config_path[] = "configs/local-dev.json";
    char* argv[] = {program, config_path};

    xs::node::NodeCommandLineArgs args;
    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result = xs::node::ParseNodeCommandLine(2, argv, &args, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::InvalidArgumentCount);
    XS_CHECK_MSG(error_message == xs::node::NodeUsage(), error_message.c_str());
}

void TestParseNodeCommandLineRejectsEmptyConfigPath()
{
    char program[] = "xserver-node";
    char config_path[] = "";
    char selector[] = "gm";
    char* argv[] = {program, config_path, selector};

    xs::node::NodeCommandLineArgs args;
    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result = xs::node::ParseNodeCommandLine(3, argv, &args, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::EmptyConfigPath);
    XS_CHECK_MSG(error_message.find("configPath") != std::string::npos, error_message.c_str());
}

void TestParseNodeCommandLineRejectsEmptySelector()
{
    char program[] = "xserver-node";
    char config_path[] = "configs/local-dev.json";
    char selector[] = "";
    char* argv[] = {program, config_path, selector};

    xs::node::NodeCommandLineArgs args;
    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result = xs::node::ParseNodeCommandLine(3, argv, &args, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::EmptySelector);
    XS_CHECK_MSG(error_message.find("selector") != std::string::npos, error_message.c_str());
}

void TestLoadNodeRuntimeContextRejectsInvalidSelector()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-invalid-selector");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::NodeCommandLineArgs args;
    args.config_path = config_path;
    args.selector = "bad";

    xs::node::NodeRuntimeContext context;
    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result = xs::node::LoadNodeRuntimeContext(args, &context, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::InvalidSelector);
    XS_CHECK_MSG(error_message.find("selector") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestLoadNodeRuntimeContextDerivesGm()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-context-gm");
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

    XS_CHECK(context.process_type == xs::core::ProcessType::Gm);
    XS_CHECK(context.selector == "gm");
    XS_CHECK(context.node_id == "GM");
    XS_CHECK(context.config_path == config_path);
    XS_CHECK(context.pid != 0U);
    XS_CHECK(context.node_config.process_type == xs::core::ProcessType::Gm);
    XS_CHECK(context.node_config.instance_id == "GM");
    XS_CHECK(context.node_config.source_path == config_path);

    CleanupTestDirectory(base_path);
}

void TestLoadNodeRuntimeContextDerivesGate()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-context-gate");
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

    XS_CHECK(context.process_type == xs::core::ProcessType::Gate);
    XS_CHECK(context.selector == "gate0");
    XS_CHECK(context.node_id == "Gate0");
    XS_CHECK(context.config_path == config_path);
    XS_CHECK(context.pid != 0U);
    XS_CHECK(context.node_config.process_type == xs::core::ProcessType::Gate);
    XS_CHECK(context.node_config.instance_id == "Gate0");
    XS_CHECK(context.node_config.source_path == config_path);

    CleanupTestDirectory(base_path);
}

void TestLoadNodeRuntimeContextDerivesGame()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-context-game");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::NodeRuntimeContext context;
    if (!LoadRuntimeContext(config_path, "game0", &context))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(context.process_type == xs::core::ProcessType::Game);
    XS_CHECK(context.selector == "game0");
    XS_CHECK(context.node_id == "Game0");
    XS_CHECK(context.config_path == config_path);
    XS_CHECK(context.pid != 0U);
    XS_CHECK(context.node_config.process_type == xs::core::ProcessType::Game);
    XS_CHECK(context.node_config.instance_id == "Game0");
    XS_CHECK(context.node_config.source_path == config_path);

    CleanupTestDirectory(base_path);
}

void VerifyRoleDispatch(
    std::string_view test_name,
    std::string_view selector,
    xs::core::ProcessType expected_process_type,
    std::string_view expected_node_id)
{
    const std::filesystem::path base_path = PrepareTestDirectory(test_name);
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::NodeRuntimeContext context;
    if (!LoadRuntimeContext(config_path, selector, &context))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    bool gm_called = false;
    bool gate_called = false;
    bool game_called = false;
    xs::core::ProcessType captured_process_type = xs::core::ProcessType::Gm;
    std::string captured_node_id;
    std::string captured_selector;
    std::filesystem::path captured_config_path;
    std::string captured_log_root;

    xs::node::NodeRoleRunners role_runners;
    role_runners.gm = [&](const xs::node::NodeRuntimeContext& runtime_context,
                          xs::core::Logger& logger,
                          xs::core::MainEventLoop& event_loop,
                          xs::node::NodeRoleRuntimeBindings* runtime_bindings,
                          std::string* error_message) {
        XS_CHECK(runtime_bindings != nullptr);
        gm_called = true;
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        captured_process_type = runtime_context.process_type;
        captured_node_id = runtime_context.node_id;
        captured_selector = runtime_context.selector;
        captured_config_path = runtime_context.config_path;
        captured_log_root = logger.options().config.root_dir;
        event_loop.RequestStop();
        return xs::node::NodeRuntimeErrorCode::None;
    };
    role_runners.gate = [&](const xs::node::NodeRuntimeContext& runtime_context,
                            xs::core::Logger& logger,
                            xs::core::MainEventLoop& event_loop,
                            xs::node::NodeRoleRuntimeBindings* runtime_bindings,
                            std::string* error_message) {
        XS_CHECK(runtime_bindings != nullptr);
        gate_called = true;
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        captured_process_type = runtime_context.process_type;
        captured_node_id = runtime_context.node_id;
        captured_selector = runtime_context.selector;
        captured_config_path = runtime_context.config_path;
        captured_log_root = logger.options().config.root_dir;
        event_loop.RequestStop();
        return xs::node::NodeRuntimeErrorCode::None;
    };
    role_runners.game = [&](const xs::node::NodeRuntimeContext& runtime_context,
                            xs::core::Logger& logger,
                            xs::core::MainEventLoop& event_loop,
                            xs::node::NodeRoleRuntimeBindings* runtime_bindings,
                            std::string* error_message) {
        XS_CHECK(runtime_bindings != nullptr);
        game_called = true;
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        captured_process_type = runtime_context.process_type;
        captured_node_id = runtime_context.node_id;
        captured_selector = runtime_context.selector;
        captured_config_path = runtime_context.config_path;
        captured_log_root = logger.options().config.root_dir;
        event_loop.RequestStop();
        return xs::node::NodeRuntimeErrorCode::None;
    };

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, role_runners, xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK_MSG(result == xs::node::NodeRuntimeErrorCode::None, error_message.c_str());
    XS_CHECK(gm_called == (expected_process_type == xs::core::ProcessType::Gm));
    XS_CHECK(gate_called == (expected_process_type == xs::core::ProcessType::Gate));
    XS_CHECK(game_called == (expected_process_type == xs::core::ProcessType::Game));
    XS_CHECK(captured_process_type == expected_process_type);
    XS_CHECK(captured_node_id == expected_node_id);
    XS_CHECK(captured_selector == selector);
    XS_CHECK(captured_config_path == config_path);
    XS_CHECK(captured_log_root == context.node_config.logging.root_dir);

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessDispatchesGm()
{
    VerifyRoleDispatch("node-runtime-dispatch-gm", "gm", xs::core::ProcessType::Gm, "GM");
}

void TestRunNodeProcessDispatchesGate()
{
    VerifyRoleDispatch("node-runtime-dispatch-gate", "gate0", xs::core::ProcessType::Gate, "Gate0");
}

void TestRunNodeProcessDispatchesGame()
{
    VerifyRoleDispatch("node-runtime-dispatch-game", "game0", xs::core::ProcessType::Game, "Game0");
}

void TestRunNodeProcessInvokesRoleStopHandler()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-stop-handler");
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

    bool stop_called = false;

    xs::node::NodeRoleRunners role_runners;
    role_runners.gm = [](
                          const xs::node::NodeRuntimeContext&,
                          xs::core::Logger&,
                          xs::core::MainEventLoop&,
                          xs::node::NodeRoleRuntimeBindings*,
                          std::string*) {
        return xs::node::NodeRuntimeErrorCode::None;
    };
    role_runners.gate = [&](const xs::node::NodeRuntimeContext&,
                            xs::core::Logger&,
                            xs::core::MainEventLoop& event_loop,
                            xs::node::NodeRoleRuntimeBindings* runtime_bindings,
                            std::string* error_message) {
        XS_CHECK(runtime_bindings != nullptr);
        if (runtime_bindings != nullptr)
        {
            runtime_bindings->on_stop = [&](xs::core::MainEventLoop&) {
                stop_called = true;
            };
        }
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        event_loop.RequestStop();
        return xs::node::NodeRuntimeErrorCode::None;
    };
    role_runners.game = role_runners.gm;

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, role_runners, xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK_MSG(result == xs::node::NodeRuntimeErrorCode::None, error_message.c_str());
    XS_CHECK(stop_called);

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessRejectsMissingRoleRunner()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-missing-runner");
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

    xs::node::NodeRoleRunners role_runners;
    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, role_runners, xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::MissingRoleRunner);
    XS_CHECK_MSG(error_message.find("GM") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessPropagatesRoleRunnerFailure()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-runner-failure");
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

    xs::node::NodeRoleRunners role_runners;
    role_runners.gm = [](
                          const xs::node::NodeRuntimeContext&,
                          xs::core::Logger&,
                          xs::core::MainEventLoop&,
                          xs::node::NodeRoleRuntimeBindings* runtime_bindings,
                          std::string* error_message) {
        (void)runtime_bindings;
        if (error_message != nullptr)
        {
            *error_message = "runner failed";
        }
        return xs::node::NodeRuntimeErrorCode::RoleRunnerFailed;
    };
    role_runners.gate = role_runners.gm;
    role_runners.game = role_runners.gm;

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, role_runners, xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::RoleRunnerFailed);
    XS_CHECK_MSG(error_message == "runner failed", error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessPropagatesEventLoopFailure()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-event-loop-failure");
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

    xs::node::NodeRoleRunners role_runners;
    role_runners.gm = [](
                          const xs::node::NodeRuntimeContext&,
                          xs::core::Logger&,
                          xs::core::MainEventLoop& event_loop,
                          xs::node::NodeRoleRuntimeBindings* runtime_bindings,
                          std::string* error_message) {
        (void)runtime_bindings;
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        event_loop.RequestStop();
        return xs::node::NodeRuntimeErrorCode::None;
    };
    role_runners.gate = role_runners.gm;
    role_runners.game = role_runners.gm;

    xs::node::NodeRuntimeRunOptions options;
    options.event_loop_options = xs::core::MainEventLoopOptions{};
    options.event_loop_options->thread_name.clear();

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result = xs::node::RunNodeProcess(context, role_runners, options, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::EventLoopFailed);
    XS_CHECK_MSG(error_message.find("thread name") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessUsesDefaultRunners()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-default-runners");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::NodeCommandLineArgs args;
    args.config_path = config_path;
    args.selector = "gate0";

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(args, xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK_MSG(result == xs::node::NodeRuntimeErrorCode::None, error_message.c_str());
    XS_CHECK(DirectoryContainsRegularFile(base_path / "logs" / "gate"));

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestParseNodeCommandLineSuccess();
    TestParseNodeCommandLineRejectsInvalidArgumentCount();
    TestParseNodeCommandLineRejectsEmptyConfigPath();
    TestParseNodeCommandLineRejectsEmptySelector();
    TestLoadNodeRuntimeContextRejectsInvalidSelector();
    TestLoadNodeRuntimeContextDerivesGm();
    TestLoadNodeRuntimeContextDerivesGate();
    TestLoadNodeRuntimeContextDerivesGame();
    TestRunNodeProcessDispatchesGm();
    TestRunNodeProcessDispatchesGate();
    TestRunNodeProcessDispatchesGame();
    TestRunNodeProcessInvokesRoleStopHandler();
    TestRunNodeProcessRejectsMissingRoleRunner();
    TestRunNodeProcessPropagatesRoleRunnerFailure();
    TestRunNodeProcessPropagatesEventLoopFailure();
    TestRunNodeProcessUsesDefaultRunners();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " node runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

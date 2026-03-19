#include "GameNode.h"
#include "GateNode.h"
#include "GmNode.h"
#include "Json.h"
#include "NodeRuntime.h"
#include "ServerNode.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
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

xs::core::LoggerOptions BuildLoggerOptions(const xs::node::NodeRuntimeContext& context)
{
    xs::core::LoggerOptions options;
    options.process_type = context.process_type;
    options.instance_id = context.node_id;
    options.config = context.node_config.logging;
    return options;
}

class TestServerNode final : public xs::node::ServerNode
{
  public:
    struct Options
    {
        xs::node::NodeRuntimeErrorCode init_result{xs::node::NodeRuntimeErrorCode::None};
        xs::node::NodeRuntimeErrorCode run_result{xs::node::NodeRuntimeErrorCode::None};
        std::string init_error{};
        std::string run_error{};
        bool request_stop_in_run{false};
    };

    TestServerNode(
        xs::node::ServerNodeEnvironment environment,
        Options options,
        bool* init_called,
        bool* run_called,
        bool* uninit_called)
        : xs::node::ServerNode(environment),
          options_(std::move(options)),
          init_called_(init_called),
          run_called_(run_called),
          uninit_called_(uninit_called)
    {
    }

    [[nodiscard]] xs::node::NodeRuntimeErrorCode Init(std::string* error_message) override
    {
        if (init_called_ != nullptr)
        {
            *init_called_ = true;
        }

        if (error_message != nullptr)
        {
            *error_message = options_.init_error;
        }

        return options_.init_result;
    }

    [[nodiscard]] xs::node::NodeRuntimeErrorCode Run(std::string* error_message) override
    {
        if (run_called_ != nullptr)
        {
            *run_called_ = true;
        }

        if (error_message != nullptr)
        {
            *error_message = options_.run_error;
        }

        if (options_.request_stop_in_run)
        {
            event_loop().RequestStop();
        }

        return options_.run_result;
    }

    void Uninit() noexcept override
    {
        if (uninit_called_ != nullptr)
        {
            *uninit_called_ = true;
        }
    }

  private:
    Options options_{};
    bool* init_called_{nullptr};
    bool* run_called_{nullptr};
    bool* uninit_called_{nullptr};
};

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

void VerifyDefaultNodeCreation(
    std::string_view test_name,
    std::string_view selector,
    bool expect_gm,
    bool expect_gate,
    bool expect_game)
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

    xs::core::Logger logger(BuildLoggerOptions(context));
    xs::core::MainEventLoop event_loop({.thread_name = "node-create-test"});
    xs::node::ServerNodePtr node;
    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::CreateServerNode(context, logger, event_loop, &node, &error_message);

    XS_CHECK_MSG(result == xs::node::NodeRuntimeErrorCode::None, error_message.c_str());
    XS_CHECK(node != nullptr);
    XS_CHECK((dynamic_cast<xs::node::GmNode*>(node.get()) != nullptr) == expect_gm);
    XS_CHECK((dynamic_cast<xs::node::GateNode*>(node.get()) != nullptr) == expect_gate);
    XS_CHECK((dynamic_cast<xs::node::GameNode*>(node.get()) != nullptr) == expect_game);

    CleanupTestDirectory(base_path);
}

void TestCreateServerNodeDispatchesGm()
{
    VerifyDefaultNodeCreation("node-runtime-create-gm", "gm", true, false, false);
}

void TestCreateServerNodeDispatchesGate()
{
    VerifyDefaultNodeCreation("node-runtime-create-gate", "gate0", false, true, false);
}

void TestCreateServerNodeDispatchesGame()
{
    VerifyDefaultNodeCreation("node-runtime-create-game", "game0", false, false, true);
}

void TestRunNodeProcessInvokesServerNodeLifecycle()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-lifecycle");
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

    bool init_called = false;
    bool run_called = false;
    bool uninit_called = false;

    xs::node::ServerNodeFactory factory = [&](const xs::node::NodeRuntimeContext& runtime_context,
                                              xs::core::Logger& logger,
                                              xs::core::MainEventLoop& event_loop,
                                              std::string* error_message) {
        if (error_message != nullptr)
        {
            error_message->clear();
        }

        XS_CHECK(runtime_context.process_type == xs::core::ProcessType::Gate);
        XS_CHECK(logger.options().instance_id == "Gate0");

        xs::node::ServerNodeEnvironment environment{
            .context = runtime_context,
            .logger = logger,
            .event_loop = event_loop,
        };
        TestServerNode::Options options;
        options.request_stop_in_run = true;
        return std::make_unique<TestServerNode>(environment, std::move(options), &init_called, &run_called, &uninit_called);
    };

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, std::move(factory), xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK_MSG(result == xs::node::NodeRuntimeErrorCode::None, error_message.c_str());
    XS_CHECK(init_called);
    XS_CHECK(run_called);
    XS_CHECK(uninit_called);

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessRejectsMissingFactory()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-missing-factory");
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

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, xs::node::ServerNodeFactory{}, xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::NodeCreateFailed);
    XS_CHECK_MSG(error_message.find("factory") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessRejectsNullServerNode()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-null-node");
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

    xs::node::ServerNodeFactory factory = [](
                                              const xs::node::NodeRuntimeContext&,
                                              xs::core::Logger&,
                                              xs::core::MainEventLoop&,
                                              std::string* error_message) -> xs::node::ServerNodePtr {
        if (error_message != nullptr)
        {
            *error_message = "factory returned null";
        }
        return nullptr;
    };

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, std::move(factory), xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::NodeCreateFailed);
    XS_CHECK_MSG(error_message == "factory returned null", error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessPropagatesNodeInitFailure()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-init-failure");
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

    bool init_called = false;
    bool run_called = false;
    bool uninit_called = false;

    xs::node::ServerNodeFactory factory = [&](const xs::node::NodeRuntimeContext& runtime_context,
                                              xs::core::Logger& logger,
                                              xs::core::MainEventLoop& event_loop,
                                              std::string* error_message) {
        if (error_message != nullptr)
        {
            error_message->clear();
        }

        xs::node::ServerNodeEnvironment environment{
            .context = runtime_context,
            .logger = logger,
            .event_loop = event_loop,
        };
        TestServerNode::Options options;
        options.init_result = xs::node::NodeRuntimeErrorCode::InvalidArgument;
        options.init_error = "init failed";
        return std::make_unique<TestServerNode>(environment, std::move(options), &init_called, &run_called, &uninit_called);
    };

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, std::move(factory), xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::NodeInitFailed);
    XS_CHECK_MSG(error_message == "init failed", error_message.c_str());
    XS_CHECK(init_called);
    XS_CHECK(!run_called);
    XS_CHECK(uninit_called);

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessPropagatesNodeRunFailure()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-run-failure");
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

    bool init_called = false;
    bool run_called = false;
    bool uninit_called = false;

    xs::node::ServerNodeFactory factory = [&](const xs::node::NodeRuntimeContext& runtime_context,
                                              xs::core::Logger& logger,
                                              xs::core::MainEventLoop& event_loop,
                                              std::string* error_message) {
        if (error_message != nullptr)
        {
            error_message->clear();
        }

        xs::node::ServerNodeEnvironment environment{
            .context = runtime_context,
            .logger = logger,
            .event_loop = event_loop,
        };
        TestServerNode::Options options;
        options.run_result = xs::node::NodeRuntimeErrorCode::InvalidArgument;
        options.run_error = "run failed";
        return std::make_unique<TestServerNode>(environment, std::move(options), &init_called, &run_called, &uninit_called);
    };

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result =
        xs::node::RunNodeProcess(context, std::move(factory), xs::node::NodeRuntimeRunOptions{}, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::NodeRunFailed);
    XS_CHECK_MSG(error_message == "run failed", error_message.c_str());
    XS_CHECK(init_called);
    XS_CHECK(run_called);
    XS_CHECK(uninit_called);

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
    if (!LoadRuntimeContext(config_path, "gate0", &context))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::ServerNodeFactory factory = [&](const xs::node::NodeRuntimeContext& runtime_context,
                                              xs::core::Logger& logger,
                                              xs::core::MainEventLoop& event_loop,
                                              std::string* error_message) {
        if (error_message != nullptr)
        {
            error_message->clear();
        }

        xs::node::ServerNodeEnvironment environment{
            .context = runtime_context,
            .logger = logger,
            .event_loop = event_loop,
        };
        TestServerNode::Options options;
        options.request_stop_in_run = true;
        return std::make_unique<TestServerNode>(environment, std::move(options), nullptr, nullptr, nullptr);
    };

    xs::node::NodeRuntimeRunOptions options;
    options.event_loop_options = xs::core::MainEventLoopOptions{};
    options.event_loop_options->thread_name.clear();

    std::string error_message;
    const xs::node::NodeRuntimeErrorCode result = xs::node::RunNodeProcess(context, std::move(factory), options, &error_message);

    XS_CHECK(result == xs::node::NodeRuntimeErrorCode::EventLoopFailed);
    XS_CHECK_MSG(error_message.find("thread name") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestRunNodeProcessUsesDefaultNodes()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-runtime-default-nodes");
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
    TestCreateServerNodeDispatchesGm();
    TestCreateServerNodeDispatchesGate();
    TestCreateServerNodeDispatchesGame();
    TestRunNodeProcessInvokesServerNodeLifecycle();
    TestRunNodeProcessRejectsMissingFactory();
    TestRunNodeProcessRejectsNullServerNode();
    TestRunNodeProcessPropagatesNodeInitFailure();
    TestRunNodeProcessPropagatesNodeRunFailure();
    TestRunNodeProcessPropagatesEventLoopFailure();
    TestRunNodeProcessUsesDefaultNodes();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " node runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

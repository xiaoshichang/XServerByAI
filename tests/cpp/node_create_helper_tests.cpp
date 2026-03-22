#include "GameNode.h"
#include "GateNode.h"
#include "GmNode.h"
#include "Json.h"
#include "NodeCreateHelper.h"
#include "ServerNode.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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
                   xs::core::Json{{"host", "127.0.0.1"}, {"port", 5000}}},
              }},
             {"controlNetwork",
              xs::core::Json{
                  {"listenEndpoint",
                   xs::core::Json{{"host", "127.0.0.1"}, {"port", 5100}}},
              }},
         }},
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

class TestServerNode final : public xs::node::ServerNode
{
  public:
    struct Options
    {
        xs::core::ProcessType expected_process_type{xs::core::ProcessType::Gate};
        xs::node::NodeErrorCode init_result{xs::node::NodeErrorCode::None};
        xs::node::NodeErrorCode run_result{xs::node::NodeErrorCode::None};
        xs::node::NodeErrorCode uninit_result{xs::node::NodeErrorCode::None};
        std::string init_error{};
        std::string run_error{};
        std::string uninit_error{};
        bool request_stop_in_run{false};
    };

    TestServerNode(
        xs::node::NodeCommandLineArgs args,
        Options options,
        bool* init_called,
        bool* run_called,
        bool* uninit_called)
        : xs::node::ServerNode(std::move(args)),
          options_(std::move(options)),
          init_called_(init_called),
          run_called_(run_called),
          uninit_called_(uninit_called)
    {
    }

    [[nodiscard]] xs::core::ProcessType observed_process_type() const noexcept
    {
        return observed_process_type_;
    }

    [[nodiscard]] std::string_view observed_node_id() const noexcept
    {
        return observed_node_id_;
    }

    [[nodiscard]] std::uint32_t observed_pid() const noexcept
    {
        return observed_pid_;
    }

    [[nodiscard]] std::string_view observed_logger_instance_id() const noexcept
    {
        return observed_logger_instance_id_;
    }

  protected:
    [[nodiscard]] xs::core::ProcessType role_process_type() const noexcept override
    {
        return options_.expected_process_type;
    }

    [[nodiscard]] xs::node::NodeErrorCode OnInit() override
    {
        if (init_called_ != nullptr)
        {
            *init_called_ = true;
        }

        observed_process_type_ = process_type();
        observed_node_id_ = std::string(node_id());
        observed_pid_ = pid();
        observed_logger_instance_id_ = logger().options().instance_id;

        if (options_.init_result != xs::node::NodeErrorCode::None)
        {
            return SetError(options_.init_result, options_.init_error);
        }

        return xs::node::NodeErrorCode::None;
    }

    [[nodiscard]] xs::node::NodeErrorCode OnRun() override
    {
        if (run_called_ != nullptr)
        {
            *run_called_ = true;
        }

        if (options_.request_stop_in_run)
        {
            event_loop().RequestStop();
        }

        if (options_.run_result != xs::node::NodeErrorCode::None)
        {
            return SetError(options_.run_result, options_.run_error);
        }

        return xs::node::NodeErrorCode::None;
    }

    [[nodiscard]] xs::node::NodeErrorCode OnUninit() override
    {
        if (uninit_called_ != nullptr)
        {
            *uninit_called_ = true;
        }

        if (options_.uninit_result != xs::node::NodeErrorCode::None)
        {
            return SetError(options_.uninit_result, options_.uninit_error);
        }

        return xs::node::NodeErrorCode::None;
    }

  private:
    Options options_{};
    bool* init_called_{nullptr};
    bool* run_called_{nullptr};
    bool* uninit_called_{nullptr};
    xs::core::ProcessType observed_process_type_{xs::core::ProcessType::Gm};
    std::string observed_node_id_{};
    std::uint32_t observed_pid_{0U};
    std::string observed_logger_instance_id_{};
};

void TestNodeCreateHelperParsesCommandLineSuccess()
{
    char program[] = "xserver-node";
    char config_path[] = "configs/local-dev.json";
    char node_id[] = "Gate0";
    char* argv[] = {program, config_path, node_id};

    xs::node::NodeCreateHelper create_helper;
    const xs::node::NodeErrorCode result = create_helper.ParseCommandLine(3, argv);

    XS_CHECK(result == xs::node::NodeErrorCode::None);
    XS_CHECK(create_helper.last_error_message().empty());
    XS_CHECK(create_helper.args().config_path == std::filesystem::path(config_path));
    XS_CHECK(create_helper.args().node_id == "Gate0");
}

void TestNodeCreateHelperRejectsInvalidArgumentCount()
{
    char program[] = "xserver-node";
    char config_path[] = "configs/local-dev.json";
    char* argv[] = {program, config_path};

    xs::node::NodeCreateHelper create_helper;
    const xs::node::NodeErrorCode result = create_helper.ParseCommandLine(2, argv);

    XS_CHECK(result == xs::node::NodeErrorCode::InvalidArgumentCount);
    XS_CHECK_MSG(create_helper.last_error_message() == xs::node::NodeUsage(), create_helper.last_error_message().data());
}

void TestNodeCreateHelperRejectsEmptyConfigPath()
{
    char program[] = "xserver-node";
    char config_path[] = "";
    char node_id[] = "GM";
    char* argv[] = {program, config_path, node_id};

    xs::node::NodeCreateHelper create_helper;
    const xs::node::NodeErrorCode result = create_helper.ParseCommandLine(3, argv);

    XS_CHECK(result == xs::node::NodeErrorCode::EmptyConfigPath);
    XS_CHECK_MSG(
        std::string(create_helper.last_error_message()).find("configPath") != std::string::npos,
        create_helper.last_error_message().data());
}

void TestNodeCreateHelperRejectsEmptyNodeId()
{
    char program[] = "xserver-node";
    char config_path[] = "configs/local-dev.json";
    char node_id[] = "";
    char* argv[] = {program, config_path, node_id};

    xs::node::NodeCreateHelper create_helper;
    const xs::node::NodeErrorCode result = create_helper.ParseCommandLine(3, argv);

    XS_CHECK(result == xs::node::NodeErrorCode::EmptyNodeId);
    XS_CHECK_MSG(
        std::string(create_helper.last_error_message()).find("nodeId") != std::string::npos,
        create_helper.last_error_message().data());
}

void TestNodeCreateHelperRejectsInvalidNodeId()
{
    xs::node::NodeCreateHelper create_helper({
        .config_path = "configs/local-dev.json",
        .node_id = "bad",
    });

    xs::node::ServerNodePtr node;
    const xs::node::NodeErrorCode result = create_helper.CreateNode(&node);

    XS_CHECK(result == xs::node::NodeErrorCode::InvalidNodeId);
    XS_CHECK(node == nullptr);
    XS_CHECK_MSG(
        std::string(create_helper.last_error_message()).find("nodeId") != std::string::npos,
        create_helper.last_error_message().data());
}

void VerifyDefaultNodeCreation(
    std::string_view node_id,
    bool expect_gm,
    bool expect_gate,
    bool expect_game)
{
    xs::node::NodeCreateHelper create_helper({
        .config_path = "configs/local-dev.json",
        .node_id = std::string(node_id),
    });

    xs::node::ServerNodePtr node;
    const xs::node::NodeErrorCode result = create_helper.CreateNode(&node);

    XS_CHECK_MSG(result == xs::node::NodeErrorCode::None, create_helper.last_error_message().data());
    XS_CHECK(node != nullptr);
    XS_CHECK((dynamic_cast<xs::node::GmNode*>(node.get()) != nullptr) == expect_gm);
    XS_CHECK((dynamic_cast<xs::node::GateNode*>(node.get()) != nullptr) == expect_gate);
    XS_CHECK((dynamic_cast<xs::node::GameNode*>(node.get()) != nullptr) == expect_game);
}

void TestNodeCreateHelperDispatchesGm()
{
    VerifyDefaultNodeCreation("GM", true, false, false);
}

void TestNodeCreateHelperDispatchesGate()
{
    VerifyDefaultNodeCreation("Gate0", false, true, false);
}

void TestNodeCreateHelperDispatchesGame()
{
    VerifyDefaultNodeCreation("Game0", false, false, true);
}

void TestServerNodeLifecycleLoadsConfigAndRunsEventLoop()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-create-helper-lifecycle");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    bool init_called = false;
    bool run_called = false;
    bool uninit_called = false;

    TestServerNode::Options options;
    options.expected_process_type = xs::core::ProcessType::Gate;
    options.request_stop_in_run = true;

    TestServerNode node({
                            .config_path = config_path,
                            .node_id = "Gate0",
                        },
                        std::move(options),
                        &init_called,
                        &run_called,
                        &uninit_called);

    const xs::node::NodeErrorCode init_result = node.Init();
    XS_CHECK_MSG(init_result == xs::node::NodeErrorCode::None, node.last_error_message().data());
    XS_CHECK(node.initialized());
    XS_CHECK(init_called);
    XS_CHECK(node.observed_process_type() == xs::core::ProcessType::Gate);
    XS_CHECK(node.observed_node_id() == "Gate0");
    XS_CHECK(node.observed_pid() != 0U);
    XS_CHECK(node.observed_logger_instance_id() == "Gate0");

    const xs::node::NodeErrorCode run_result = node.Run();
    XS_CHECK_MSG(run_result == xs::node::NodeErrorCode::None, node.last_error_message().data());
    XS_CHECK(run_called);

    const xs::node::NodeErrorCode uninit_result = node.Uninit();
    XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node.last_error_message().data());
    XS_CHECK(uninit_called);
    XS_CHECK(!node.initialized());

    CleanupTestDirectory(base_path);
}

void TestServerNodeRejectsProcessTypeMismatch()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-create-helper-process-mismatch");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    bool init_called = false;
    TestServerNode::Options options;
    options.expected_process_type = xs::core::ProcessType::Game;

    TestServerNode node({
                            .config_path = config_path,
                            .node_id = "Gate0",
                        },
                        std::move(options),
                        &init_called,
                        nullptr,
                        nullptr);

    const xs::node::NodeErrorCode init_result = node.Init();

    XS_CHECK(init_result == xs::node::NodeErrorCode::InvalidArgument);
    XS_CHECK(!init_called);
    XS_CHECK_MSG(
        std::string(node.last_error_message()).find("Game node requires nodeId resolving to Game.") !=
            std::string::npos,
        node.last_error_message().data());

    CleanupTestDirectory(base_path);
}

void TestServerNodePropagatesInitFailureAndCleansUp()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-create-helper-init-failure");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    bool init_called = false;
    bool run_called = false;
    bool uninit_called = false;

    TestServerNode::Options options;
    options.expected_process_type = xs::core::ProcessType::Gate;
    options.init_result = xs::node::NodeErrorCode::InvalidArgument;
    options.init_error = "init failed";

    TestServerNode node({
                            .config_path = config_path,
                            .node_id = "Gate0",
                        },
                        std::move(options),
                        &init_called,
                        &run_called,
                        &uninit_called);

    const xs::node::NodeErrorCode init_result = node.Init();

    XS_CHECK(init_result == xs::node::NodeErrorCode::InvalidArgument);
    XS_CHECK(init_called);
    XS_CHECK(!run_called);
    XS_CHECK(uninit_called);
    XS_CHECK(!node.initialized());
    XS_CHECK_MSG(node.last_error_message() == "init failed", node.last_error_message().data());

    CleanupTestDirectory(base_path);
}

void TestServerNodePropagatesRunFailure()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-create-helper-run-failure");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    bool init_called = false;
    bool run_called = false;
    bool uninit_called = false;

    TestServerNode::Options options;
    options.expected_process_type = xs::core::ProcessType::Gate;
    options.run_result = xs::node::NodeErrorCode::InvalidArgument;
    options.run_error = "run failed";

    TestServerNode node({
                            .config_path = config_path,
                            .node_id = "Gate0",
                        },
                        std::move(options),
                        &init_called,
                        &run_called,
                        &uninit_called);

    const xs::node::NodeErrorCode init_result = node.Init();
    XS_CHECK_MSG(init_result == xs::node::NodeErrorCode::None, node.last_error_message().data());

    const xs::node::NodeErrorCode run_result = node.Run();
    XS_CHECK(run_result == xs::node::NodeErrorCode::InvalidArgument);
    XS_CHECK(init_called);
    XS_CHECK(run_called);
    XS_CHECK(!uninit_called);
    XS_CHECK_MSG(node.last_error_message() == "run failed", node.last_error_message().data());

    const xs::node::NodeErrorCode uninit_result = node.Uninit();
    XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node.last_error_message().data());
    XS_CHECK(uninit_called);

    CleanupTestDirectory(base_path);
}

void TestDefaultNodeLifecycleUsesCreateHelper()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-create-helper-default-node");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::NodeCreateHelper create_helper({
        .config_path = config_path,
        .node_id = "Gate0",
    });

    xs::node::ServerNodePtr node;
    const xs::node::NodeErrorCode create_result = create_helper.CreateNode(&node);
    XS_CHECK_MSG(create_result == xs::node::NodeErrorCode::None, create_helper.last_error_message().data());
    XS_CHECK(node != nullptr);

    const xs::node::NodeErrorCode init_result = node->Init();
    XS_CHECK_MSG(init_result == xs::node::NodeErrorCode::None, node->last_error_message().data());

    const xs::node::NodeErrorCode run_result = node->Run();
    XS_CHECK_MSG(run_result == xs::node::NodeErrorCode::None, node->last_error_message().data());

    const xs::node::NodeErrorCode uninit_result = node->Uninit();
    XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node->last_error_message().data());
    XS_CHECK(DirectoryContainsRegularFile(base_path / "logs"));

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestNodeCreateHelperParsesCommandLineSuccess();
    TestNodeCreateHelperRejectsInvalidArgumentCount();
    TestNodeCreateHelperRejectsEmptyConfigPath();
    TestNodeCreateHelperRejectsEmptyNodeId();
    TestNodeCreateHelperRejectsInvalidNodeId();
    TestNodeCreateHelperDispatchesGm();
    TestNodeCreateHelperDispatchesGate();
    TestNodeCreateHelperDispatchesGame();
    TestServerNodeLifecycleLoadsConfigAndRunsEventLoop();
    TestServerNodeRejectsProcessTypeMismatch();
    TestServerNodePropagatesInitFailureAndCleansUp();
    TestServerNodePropagatesRunFailure();
    TestDefaultNodeLifecycleUsesCreateHelper();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " node create helper test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include "GameNode.h"
#include "GateNode.h"
#include "GmNode.h"
#include "Json.h"
#include "TestManagedConfigJson.h"

#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

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

xs::core::Json MakeClusterConfigJson(
    const std::filesystem::path& base_path,
    bool include_gm_inner_endpoint,
    bool include_game_inner_endpoint,
    std::uint16_t gm_inner_port,
    std::uint16_t gm_control_port)
{
    const std::string root_log_dir = (base_path / "logs").string();

    xs::core::Json gm_block = xs::core::Json::object();
    if (include_gm_inner_endpoint)
    {
        gm_block["innerNetwork"] = xs::core::Json{
            {"listenEndpoint", {{"host", "127.0.0.1"}, {"port", gm_inner_port}}},
        };
    }
    gm_block["controlNetwork"] = xs::core::Json{
        {"listenEndpoint", {{"host", "127.0.0.1"}, {"port", gm_control_port}}},
    };

    xs::core::Json game_instance = xs::core::Json::object();
    if (include_game_inner_endpoint)
    {
        game_instance["innerNetwork"] = xs::core::Json{
            {"listenEndpoint", {{"host", "127.0.0.1"}, {"port", 7100}}},
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
        {"managed", xs::tests::MakeManagedConfigJson()},
        {"gm", gm_block},
        {"gate",
         xs::core::Json{
             {"Gate0",
              xs::core::Json{
                  {"innerNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "127.0.0.1"}, {"port", 7000}}},
                   }},
                  {"clientNetwork",
                   xs::core::Json{
                       {"listenEndpoint",
                        xs::core::Json{{"host", "0.0.0.0"}, {"port", 4000}}},
                   }},
              }},
         }},
        {"game", xs::core::Json{{"Game0", game_instance}}},
    };
}

bool WriteRuntimeConfig(
    const std::filesystem::path& base_path,
    bool include_gm_inner_endpoint,
    bool include_game_inner_endpoint,
    std::uint16_t gm_inner_port,
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
        MakeClusterConfigJson(
            base_path,
            include_gm_inner_endpoint,
            include_game_inner_endpoint,
            gm_inner_port,
            gm_control_port));
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

    void StopAndJoin()
    {
        node_.RequestStop();
        if (run_thread_.joinable())
        {
            run_thread_.join();
        }
    }

    bool Uninit()
    {
        const xs::node::NodeErrorCode uninit_result = node_.Uninit();
        XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node_.last_error_message().data());
        return uninit_result == xs::node::NodeErrorCode::None;
    }

    [[nodiscard]] xs::node::NodeErrorCode run_result() const noexcept
    {
        return run_result_;
    }

    [[nodiscard]] std::string_view run_error() const noexcept
    {
        return run_error_;
    }

  private:
    xs::node::GmNode node_;
    std::thread run_thread_{};
    xs::node::NodeErrorCode run_result_{xs::node::NodeErrorCode::None};
    std::string run_error_{};
};

class RunningGameNode final
{
  public:
    explicit RunningGameNode(const std::filesystem::path& config_path)
        : node_({
              .config_path = config_path,
              .node_id = "Game0",
          })
    {
    }

    ~RunningGameNode()
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
            run_completed_.store(true);
        });
        return true;
    }

    void StopAndJoin()
    {
        node_.RequestStop();
        if (run_thread_.joinable())
        {
            run_thread_.join();
        }
    }

    bool Uninit()
    {
        const xs::node::NodeErrorCode uninit_result = node_.Uninit();
        XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node_.last_error_message().data());
        return uninit_result == xs::node::NodeErrorCode::None;
    }

    [[nodiscard]] xs::node::GameNode& node() noexcept
    {
        return node_;
    }

    [[nodiscard]] bool run_completed() const noexcept
    {
        return run_completed_.load();
    }

    [[nodiscard]] xs::node::NodeErrorCode run_result() const noexcept
    {
        return run_result_;
    }

    [[nodiscard]] std::string_view run_error() const noexcept
    {
        return run_error_;
    }

  private:
    xs::node::GameNode node_;
    std::thread run_thread_{};
    std::atomic_bool run_completed_{false};
    xs::node::NodeErrorCode run_result_{xs::node::NodeErrorCode::None};
    std::string run_error_{};
};

class RunningGateNode final
{
  public:
    explicit RunningGateNode(const std::filesystem::path& config_path)
        : node_({
              .config_path = config_path,
              .node_id = "Gate0",
          })
    {
    }

    ~RunningGateNode()
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

    void StopAndJoin()
    {
        node_.RequestStop();
        if (run_thread_.joinable())
        {
            run_thread_.join();
        }
    }

    bool Uninit()
    {
        const xs::node::NodeErrorCode uninit_result = node_.Uninit();
        XS_CHECK_MSG(uninit_result == xs::node::NodeErrorCode::None, node_.last_error_message().data());
        return uninit_result == xs::node::NodeErrorCode::None;
    }

    [[nodiscard]] xs::node::GateNode& node() noexcept
    {
        return node_;
    }

    [[nodiscard]] xs::node::NodeErrorCode run_result() const noexcept
    {
        return run_result_;
    }

    [[nodiscard]] std::string_view run_error() const noexcept
    {
        return run_error_;
    }

  private:
    xs::node::GateNode node_;
    std::thread run_thread_{};
    xs::node::NodeErrorCode run_result_{xs::node::NodeErrorCode::None};
    std::string run_error_{};
};

void TestGameNodeRejectsMissingGmInnerNetworkEndpointConfig()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-node-missing-gm-inner");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, false, true, 5000u, 5100u, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::GameNode node({
        .config_path = config_path,
        .node_id = "Game0",
    });

    const xs::node::NodeErrorCode result = node.Init();

    XS_CHECK(result == xs::node::NodeErrorCode::ConfigLoadFailed);
    XS_CHECK_MSG(
        std::string(node.last_error_message()).find("gm.innerNetwork") != std::string::npos,
        node.last_error_message().data());

    CleanupTestDirectory(base_path);
}

void TestGameNodeRejectsMissingGameInnerNetworkEndpointConfig()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-node-missing-game-inner");
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, true, false, 5000u, 5100u, &config_path))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::node::GameNode node({
        .config_path = config_path,
        .node_id = "Game0",
    });

    const xs::node::NodeErrorCode result = node.Init();

    XS_CHECK(result == xs::node::NodeErrorCode::ConfigLoadFailed);
    XS_CHECK_MSG(
        std::string(node.last_error_message()).find("game.Game0.innerNetwork") != std::string::npos,
        node.last_error_message().data());

    CleanupTestDirectory(base_path);
}

void TestGameNodeConnectsToGmAndStopsCleanly()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-game-node-connects-to-gm");
    const std::uint16_t gm_inner_port = AcquireLoopbackPort();
    const std::uint16_t gm_control_port = AcquireLoopbackPort();
    std::filesystem::path config_path;
    if (!WriteRuntimeConfig(base_path, true, true, gm_inner_port, gm_control_port, &config_path))
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

    RunningGateNode gate_node(config_path);
    if (!gate_node.Start())
    {
        gm_node.StopAndJoin();
        (void)gm_node.Uninit();
        CleanupTestDirectory(base_path);
        return;
    }

    RunningGameNode game_node(config_path);
    if (!game_node.Start())
    {
        gate_node.StopAndJoin();
        (void)gate_node.Uninit();
        gm_node.StopAndJoin();
        (void)gm_node.Uninit();
        CleanupTestDirectory(base_path);
        return;
    }

    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Connected;
    }));
    XS_CHECK(WaitUntil(std::chrono::seconds(2), [&game_node]() {
        return game_node.node().inner_connection_state("Gate0") == xs::ipc::ZmqConnectionState::Connected;
    }));

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    XS_CHECK(!game_node.run_completed());
    XS_CHECK(game_node.node().managed_assembly_name() == "XServer.Managed.GameLogic");
    XS_CHECK(game_node.node().inner_connection_state("Gate0") == xs::ipc::ZmqConnectionState::Connected);

    game_node.StopAndJoin();
    XS_CHECK_MSG(game_node.run_result() == xs::node::NodeErrorCode::None, game_node.run_error().data());
    XS_CHECK(game_node.Uninit());
    XS_CHECK(game_node.node().gm_inner_connection_state() == xs::ipc::ZmqConnectionState::Stopped);
    XS_CHECK(game_node.node().inner_connection_state("Gate0") == xs::ipc::ZmqConnectionState::Stopped);

    gate_node.StopAndJoin();
    XS_CHECK_MSG(gate_node.run_result() == xs::node::NodeErrorCode::None, gate_node.run_error().data());
    XS_CHECK(gate_node.Uninit());

    gm_node.StopAndJoin();
    XS_CHECK_MSG(gm_node.run_result() == xs::node::NodeErrorCode::None, gm_node.run_error().data());
    XS_CHECK(gm_node.Uninit());

    const std::filesystem::path log_dir = base_path / "logs";
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Game node configured runtime skeleton.") != std::string::npos);
    XS_CHECK(log_text.find("Inner network active connector started.") != std::string::npos);
    XS_CHECK(log_text.find("Inner network active connector state changed.") != std::string::npos);
    XS_CHECK(log_text.find("Game node entered runtime state.") != std::string::npos);
    XS_CHECK(log_text.find("gmInnerRemoteEndpoint") == std::string::npos);
    XS_CHECK(log_text.find("configuredInnerEndpoint") == std::string::npos);
    XS_CHECK(log_text.find("gateNodeId=Gate0") == std::string::npos);
    XS_CHECK(log_text.find("managedAssemblyName=XServer.Managed.GameLogic") == std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestGameNodeRejectsMissingGmInnerNetworkEndpointConfig();
    TestGameNodeRejectsMissingGameInnerNetworkEndpointConfig();
    TestGameNodeConnectsToGmAndStopsCleanly();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " Game node test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

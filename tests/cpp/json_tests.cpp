#include "Config.h"
#include "Json.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{

struct TestConfig
{
    std::string name;
    int port{};
    bool enabled{};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TestConfig, name, port, enabled)

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

bool LoadClusterConfigForTest(
    const std::filesystem::path& path,
    xs::core::ClusterConfig* output,
    std::string* error_message)
{
    const xs::core::ConfigErrorCode result = xs::core::LoadClusterConfigFile(path, output, error_message);
    XS_CHECK_MSG(result == xs::core::ConfigErrorCode::None, error_message != nullptr ? error_message->c_str() : "");
    return result == xs::core::ConfigErrorCode::None;
}

xs::core::Json MakeValidClusterConfigJson()
{
    return xs::core::Json{
        {"env", xs::core::Json{{"id", "local-dev"}, {"environment", "dev"}}},
        {"logging",
         xs::core::Json{
             {"rootDir", "logs"},
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

void TestTryParseJsonSuccess()
{
    xs::core::Json value;
    std::string error_message{"not-cleared"};

    const xs::core::JsonErrorCode success =
        xs::core::TryParseJson(R"({"service":"gate","index":3})", &value, &error_message);

    XS_CHECK(success == xs::core::JsonErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(value.is_object());
    XS_CHECK(value.at("service") == "gate");
    XS_CHECK(value.at("index") == 3);
}

void TestTryParseJsonFailure()
{
    xs::core::Json value;
    std::string error_message;

    const xs::core::JsonErrorCode success = xs::core::TryParseJson(R"({"service":)", &value, &error_message);

    XS_CHECK(success == xs::core::JsonErrorCode::ParseFailed);
    XS_CHECK(!error_message.empty());
    XS_CHECK_MSG(error_message.find("Failed to parse JSON:") != std::string::npos, error_message.c_str());
}

void TestDeserializeTypedConfig()
{
    TestConfig config{};
    std::string error_message;

    const xs::core::JsonErrorCode success = xs::core::TryParseJsonAs(
        R"({"name":"game0","port":40100,"enabled":true})",
        &config,
        &error_message);

    XS_CHECK(success == xs::core::JsonErrorCode::None);
    XS_CHECK(error_message.empty());
    XS_CHECK(config.name == "game0");
    XS_CHECK(config.port == 40100);
    XS_CHECK(config.enabled);
}

void TestSaveAndLoadJsonFileRoundTrip()
{
    const std::filesystem::path base_path = PrepareTestDirectory("json-runtime-tests");
    const std::filesystem::path file_path = base_path / "config.json";

    const TestConfig expected_config{"gate0", 33001, true};
    std::string save_error;
    const xs::core::JsonErrorCode save_success = xs::core::SaveJsonFileFrom(file_path, expected_config, &save_error);

    XS_CHECK(save_success == xs::core::JsonErrorCode::None);
    XS_CHECK(save_error.empty());
    XS_CHECK(std::filesystem::exists(file_path));

    TestConfig actual_config{};
    std::string load_error;
    const xs::core::JsonErrorCode load_success = xs::core::TryLoadJsonFileAs(file_path, &actual_config, &load_error);

    XS_CHECK(load_success == xs::core::JsonErrorCode::None);
    XS_CHECK(load_error.empty());
    XS_CHECK(actual_config.name == expected_config.name);
    XS_CHECK(actual_config.port == expected_config.port);
    XS_CHECK(actual_config.enabled == expected_config.enabled);

    CleanupTestDirectory(base_path);
}

void TestSaveJsonFileRejectsInvalidIndent()
{
    const std::filesystem::path file_path =
        std::filesystem::current_path() / "test-output" / "json-runtime-tests-invalid" / "config.json";

    std::string error_message;
    const xs::core::JsonErrorCode success = xs::core::SaveJsonFile(
        file_path,
        xs::core::Json{{"name", "gm"}},
        &error_message,
        -2);

    XS_CHECK(success == xs::core::JsonErrorCode::InvalidIndent);
    XS_CHECK(!error_message.empty());
    XS_CHECK_MSG(
        error_message.find("JSON indentation must be -1 or greater.") != std::string::npos,
        error_message.c_str());
}

void TestSelectNodeConfigRejectsInvalidNodeId()
{
    const std::filesystem::path base_path = PrepareTestDirectory("config-invalid-node-id");
    const std::filesystem::path file_path = base_path / "config.json";
    const xs::core::Json config_json = MakeValidClusterConfigJson();
    if (!WriteJsonFile(file_path, config_json))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::ClusterConfig cluster_config;
    std::string error_message;
    if (!LoadClusterConfigForTest(file_path, &cluster_config, &error_message))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::unique_ptr<xs::core::NodeConfig> node_config;
    const xs::core::ConfigErrorCode result =
        xs::core::SelectNodeConfig(cluster_config, "bad-node-id", &node_config, &error_message);

    XS_CHECK(result == xs::core::ConfigErrorCode::InvalidNodeId);
    XS_CHECK(node_config == nullptr);
    XS_CHECK_MSG(
        error_message.find("nodeId must be one of GM, Gate<index>, or Game<index>.") != std::string::npos,
        error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigForGm()
{
    const std::filesystem::path base_path = PrepareTestDirectory("config-gm");
    const std::filesystem::path file_path = base_path / "config.json";
    const xs::core::Json config_json = MakeValidClusterConfigJson();
    if (!WriteJsonFile(file_path, config_json))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::ClusterConfig cluster_config;
    std::string error_message;
    if (!LoadClusterConfigForTest(file_path, &cluster_config, &error_message))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::unique_ptr<xs::core::NodeConfig> node_config;
    const xs::core::ConfigErrorCode success =
        xs::core::SelectNodeConfig(cluster_config, "GM", &node_config, &error_message);

    XS_CHECK_MSG(success == xs::core::ConfigErrorCode::None, error_message.c_str());
    XS_CHECK(node_config != nullptr);
    XS_CHECK(cluster_config.env.id == "local-dev");
    XS_CHECK(cluster_config.env.environment == "dev");
    XS_CHECK(cluster_config.logging.root_dir == "logs");
    XS_CHECK(cluster_config.logging.min_level == xs::core::LogLevel::Info);

    const auto* gm_node_config = dynamic_cast<const xs::core::GmNodeConfig*>(node_config.get());
    XS_CHECK(gm_node_config != nullptr);
    if (gm_node_config != nullptr)
    {
        XS_CHECK(gm_node_config->inner_network_listen_endpoint.host == "127.0.0.1");
        XS_CHECK(gm_node_config->inner_network_listen_endpoint.port == 5000);
    }

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigForGate()
{
    const std::filesystem::path base_path = PrepareTestDirectory("config-gate");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    if (!WriteJsonFile(file_path, config_json))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::ClusterConfig cluster_config;
    std::string error_message;
    if (!LoadClusterConfigForTest(file_path, &cluster_config, &error_message))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::unique_ptr<xs::core::NodeConfig> node_config;
    const xs::core::ConfigErrorCode success =
        xs::core::SelectNodeConfig(cluster_config, "Gate0", &node_config, &error_message);

    XS_CHECK_MSG(success == xs::core::ConfigErrorCode::None, error_message.c_str());
    XS_CHECK(node_config != nullptr);
    XS_CHECK(cluster_config.logging.root_dir == "logs");
    XS_CHECK(cluster_config.logging.min_level == xs::core::LogLevel::Info);

    const auto* gate_node_config = dynamic_cast<const xs::core::GateNodeConfig*>(node_config.get());
    XS_CHECK(gate_node_config != nullptr);
    if (gate_node_config != nullptr)
    {
        XS_CHECK(gate_node_config->inner_network_listen_endpoint.host == "0.0.0.0");
        XS_CHECK(gate_node_config->inner_network_listen_endpoint.port == 7000);
        XS_CHECK(gate_node_config->client_network_listen_endpoint.host == "0.0.0.0");
        XS_CHECK(gate_node_config->client_network_listen_endpoint.port == 4000);
    }
    XS_CHECK(cluster_config.kcp.sndwnd == 256);
    XS_CHECK(cluster_config.kcp.mtu == 1200);
    XS_CHECK(cluster_config.kcp.interval_ms == 10);

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigForGame()
{
    const std::filesystem::path base_path = PrepareTestDirectory("config-game");
    const std::filesystem::path file_path = base_path / "config.json";
    const xs::core::Json config_json = MakeValidClusterConfigJson();
    if (!WriteJsonFile(file_path, config_json))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::ClusterConfig cluster_config;
    std::string error_message;
    if (!LoadClusterConfigForTest(file_path, &cluster_config, &error_message))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    std::unique_ptr<xs::core::NodeConfig> node_config;
    const xs::core::ConfigErrorCode success =
        xs::core::SelectNodeConfig(cluster_config, "Game0", &node_config, &error_message);

    XS_CHECK_MSG(success == xs::core::ConfigErrorCode::None, error_message.c_str());
    XS_CHECK(node_config != nullptr);
    XS_CHECK(cluster_config.logging.root_dir == "logs");
    XS_CHECK(cluster_config.logging.min_level == xs::core::LogLevel::Info);

    const auto* game_node_config = dynamic_cast<const xs::core::GameNodeConfig*>(node_config.get());
    XS_CHECK(game_node_config != nullptr);
    if (game_node_config != nullptr)
    {
        XS_CHECK(game_node_config->inner_network_listen_endpoint.host == "127.0.0.1");
        XS_CHECK(game_node_config->inner_network_listen_endpoint.port == 7100);
        XS_CHECK(game_node_config->managed.assembly_name == "XServer.Managed.GameLogic");
    }

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigRejectsUnknownTopLevelField()
{
    const std::filesystem::path base_path = PrepareTestDirectory("config-unknown-top-level");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    config_json["unexpected"] = xs::core::Json::object();
    if (!WriteJsonFile(file_path, config_json))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::ClusterConfig cluster_config;
    std::string error_message;
    const xs::core::ConfigErrorCode success =
        xs::core::LoadClusterConfigFile(file_path, &cluster_config, &error_message);

    XS_CHECK(success == xs::core::ConfigErrorCode::UnknownField);
    XS_CHECK_MSG(error_message.find("unexpected") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigRejectsUnknownLoggingField()
{
    const std::filesystem::path base_path = PrepareTestDirectory("config-unknown-logging");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    config_json["logging"]["unexpectedField"] = 1;
    if (!WriteJsonFile(file_path, config_json))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::ClusterConfig cluster_config;
    std::string error_message;
    const xs::core::ConfigErrorCode success =
        xs::core::LoadClusterConfigFile(file_path, &cluster_config, &error_message);

    XS_CHECK(success == xs::core::ConfigErrorCode::UnknownField);
    XS_CHECK_MSG(error_message.find("unexpectedField") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigRejectsInvalidGateNodeId()
{
    const std::filesystem::path base_path = PrepareTestDirectory("config-invalid-gate-node-id");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    config_json["gate"]["gate0"] = config_json["gate"]["Gate0"];
    config_json["gate"].erase("Gate0");
    if (!WriteJsonFile(file_path, config_json))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::ClusterConfig cluster_config;
    std::string error_message;
    const xs::core::ConfigErrorCode success =
        xs::core::LoadClusterConfigFile(file_path, &cluster_config, &error_message);

    XS_CHECK(success == xs::core::ConfigErrorCode::InvalidNodeId);
    XS_CHECK_MSG(error_message.find("gate.gate0") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigRejectsMissingGateClientNetwork()
{
    const std::filesystem::path base_path = PrepareTestDirectory("config-missing-gate-client-network");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    config_json["gate"]["Gate0"].erase("clientNetwork");
    if (!WriteJsonFile(file_path, config_json))
    {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::ClusterConfig cluster_config;
    std::string error_message;
    const xs::core::ConfigErrorCode success =
        xs::core::LoadClusterConfigFile(file_path, &cluster_config, &error_message);

    XS_CHECK(success == xs::core::ConfigErrorCode::MissingRequiredField);
    XS_CHECK_MSG(error_message.find("gate.Gate0.clientNetwork") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestTryParseJsonSuccess();
    TestTryParseJsonFailure();
    TestDeserializeTypedConfig();
    TestSaveAndLoadJsonFileRoundTrip();
    TestSaveJsonFileRejectsInvalidIndent();
    TestSelectNodeConfigRejectsInvalidNodeId();
    TestLoadNodeConfigForGm();
    TestLoadNodeConfigForGate();
    TestLoadNodeConfigForGame();
    TestLoadNodeConfigRejectsUnknownTopLevelField();
    TestLoadNodeConfigRejectsUnknownLoggingField();
    TestLoadNodeConfigRejectsInvalidGateNodeId();
    TestLoadNodeConfigRejectsMissingGateClientNetwork();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " JSON runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

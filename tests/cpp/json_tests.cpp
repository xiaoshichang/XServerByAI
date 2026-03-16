#include "Config.h"
#include "Json.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

struct TestConfig {
    std::string name;
    int port{};
    bool enabled{};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(TestConfig, name, port, enabled)

int g_failures = 0;

void Check(bool condition, const char* expression, const char* message = nullptr) {
    if (condition) {
        return;
    }

    std::cerr << "Check failed: " << expression;
    if (message != nullptr) {
        std::cerr << " (" << message << ")";
    }
    std::cerr << '\n';
    ++g_failures;
}

#define XS_CHECK(expr) Check((expr), #expr)
#define XS_CHECK_MSG(expr, message) Check((expr), #expr, (message))

std::filesystem::path PrepareTestDirectory(std::string_view name) {
    const std::filesystem::path path = std::filesystem::current_path() / "test-output" / std::string(name);
    std::error_code error_code;
    std::filesystem::remove_all(path, error_code);
    std::filesystem::create_directories(path, error_code);
    return path;
}

void CleanupTestDirectory(const std::filesystem::path& path) {
    std::error_code error_code;
    std::filesystem::remove_all(path, error_code);
}

bool WriteJsonFile(const std::filesystem::path& path, const xs::core::Json& value) {
    std::string error_message;
    const bool success = xs::core::SaveJsonFile(path, value, &error_message);
    XS_CHECK_MSG(success, error_message.c_str());
    return success;
}

xs::core::Json MakeValidClusterConfigJson() {
    return xs::core::Json{
        {"serverGroup", {{"id", "local-dev"}, {"environment", "dev"}}},
        {"logging",
         {{"rootDir", "logs"},
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
            {"logging", {{"minLevel", "Debug"}, {"rootDir", "logs/gate"}}}}}}},
        {"game",
         {{"game0",
           {{"nodeId", "Game0"},
            {"service", {{"listenEndpoint", {{"host", "127.0.0.1"}, {"port", 7100}}}}},
            {"logging", {{"rootDir", "logs/game"}}}}}}},
    };
}

void TestTryParseJsonSuccess() {
    xs::core::Json value;
    std::string error_message{"not-cleared"};

    const bool success = xs::core::TryParseJson(R"({"service":"gate","index":3})", &value, &error_message);

    XS_CHECK(success);
    XS_CHECK(error_message.empty());
    XS_CHECK(value.is_object());
    XS_CHECK(value.at("service") == "gate");
    XS_CHECK(value.at("index") == 3);
}

void TestTryParseJsonFailure() {
    xs::core::Json value;
    std::string error_message;

    const bool success = xs::core::TryParseJson(R"({"service":)", &value, &error_message);

    XS_CHECK(!success);
    XS_CHECK(!error_message.empty());
    XS_CHECK_MSG(error_message.find("Failed to parse JSON:") != std::string::npos, error_message.c_str());
}

void TestDeserializeTypedConfig() {
    TestConfig config{};
    std::string error_message;

    const bool success = xs::core::TryParseJsonAs(
        R"({"name":"game0","port":40100,"enabled":true})",
        &config,
        &error_message);

    XS_CHECK(success);
    XS_CHECK(error_message.empty());
    XS_CHECK(config.name == "game0");
    XS_CHECK(config.port == 40100);
    XS_CHECK(config.enabled);
}

void TestSaveAndLoadJsonFileRoundTrip() {
    const std::filesystem::path base_path = PrepareTestDirectory("json-runtime-tests");
    const std::filesystem::path file_path = base_path / "config.json";

    const TestConfig expected_config{"gate0", 33001, true};
    std::string save_error;
    const bool save_success = xs::core::SaveJsonFileFrom(file_path, expected_config, &save_error);

    XS_CHECK(save_success);
    XS_CHECK(save_error.empty());
    XS_CHECK(std::filesystem::exists(file_path));

    TestConfig actual_config{};
    std::string load_error;
    const bool load_success = xs::core::TryLoadJsonFileAs(file_path, &actual_config, &load_error);

    XS_CHECK(load_success);
    XS_CHECK(load_error.empty());
    XS_CHECK(actual_config.name == expected_config.name);
    XS_CHECK(actual_config.port == expected_config.port);
    XS_CHECK(actual_config.enabled == expected_config.enabled);

    CleanupTestDirectory(base_path);
}

void TestSaveJsonFileRejectsInvalidIndent() {
    const std::filesystem::path file_path =
        std::filesystem::current_path() / "test-output" / "json-runtime-tests-invalid" / "config.json";

    std::string error_message;
    const bool success = xs::core::SaveJsonFile(
        file_path,
        xs::core::Json{{"name", "gm"}},
        &error_message,
        -2);

    XS_CHECK(!success);
    XS_CHECK(!error_message.empty());
    XS_CHECK_MSG(
        error_message.find("JSON indentation must be -1 or greater.") != std::string::npos,
        error_message.c_str());
}

void TestParseNodeSelector() {
    const auto gate_selector = xs::core::ParseNodeSelector("gate12");
    XS_CHECK(gate_selector.has_value());
    XS_CHECK(gate_selector->kind == xs::core::NodeSelectorKind::Gate);
    XS_CHECK(xs::core::SelectorCanonicalNodeId(*gate_selector) == "Gate12");
    XS_CHECK(!xs::core::ParseNodeSelector("gate").has_value());
    XS_CHECK(!xs::core::ParseNodeSelector("bad-selector").has_value());
}

void TestLoadNodeConfigForGm() {
    const std::filesystem::path base_path = PrepareTestDirectory("config-gm");
    const std::filesystem::path file_path = base_path / "config.json";
    const xs::core::Json config_json = MakeValidClusterConfigJson();
    if (!WriteJsonFile(file_path, config_json)) {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::NodeConfig node_config;
    std::string error_message;
    const bool success = xs::core::LoadNodeConfigFile(file_path, "gm", &node_config, &error_message);

    XS_CHECK_MSG(success, error_message.c_str());
    XS_CHECK(node_config.process_type == xs::core::ProcessType::Gm);
    XS_CHECK(node_config.selector == "gm");
    XS_CHECK(node_config.instance_id == "GM");
    XS_CHECK(node_config.server_group.id == "local-dev");
    XS_CHECK(node_config.server_group.environment == "dev");
    XS_CHECK(node_config.control_listen_endpoint.has_value());
    XS_CHECK(node_config.control_listen_endpoint->host == "127.0.0.1");
    XS_CHECK(node_config.control_listen_endpoint->port == 5000);
    XS_CHECK(!node_config.service_listen_endpoint.has_value());
    XS_CHECK(node_config.logging.root_dir == "logs");
    XS_CHECK(node_config.logging.min_level == xs::core::LogLevel::Info);

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigForGate() {
    const std::filesystem::path base_path = PrepareTestDirectory("config-gate");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    if (!WriteJsonFile(file_path, config_json)) {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::NodeConfig node_config;
    std::string error_message;
    const bool success = xs::core::LoadNodeConfigFile(file_path, "gate0", &node_config, &error_message);

    XS_CHECK_MSG(success, error_message.c_str());
    XS_CHECK(node_config.process_type == xs::core::ProcessType::Gate);
    XS_CHECK(node_config.selector == "gate0");
    XS_CHECK(node_config.instance_id == "Gate0");
    XS_CHECK(node_config.service_listen_endpoint.has_value());
    XS_CHECK(node_config.service_listen_endpoint->host == "0.0.0.0");
    XS_CHECK(node_config.service_listen_endpoint->port == 7000);
    XS_CHECK(node_config.kcp.has_value());
    XS_CHECK(node_config.kcp->sndwnd == 256);
    XS_CHECK(node_config.kcp->mtu == 1200);
    XS_CHECK(node_config.kcp->interval_ms == 10);
    XS_CHECK(node_config.logging.root_dir == "logs/gate");
    XS_CHECK(node_config.logging.min_level == xs::core::LogLevel::Debug);

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigForGame() {
    const std::filesystem::path base_path = PrepareTestDirectory("config-game");
    const std::filesystem::path file_path = base_path / "config.json";
    const xs::core::Json config_json = MakeValidClusterConfigJson();
    if (!WriteJsonFile(file_path, config_json)) {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::NodeConfig node_config;
    std::string error_message;
    const bool success = xs::core::LoadNodeConfigFile(file_path, "game0", &node_config, &error_message);

    XS_CHECK_MSG(success, error_message.c_str());
    XS_CHECK(node_config.process_type == xs::core::ProcessType::Game);
    XS_CHECK(node_config.selector == "game0");
    XS_CHECK(node_config.instance_id == "Game0");
    XS_CHECK(node_config.service_listen_endpoint.has_value());
    XS_CHECK(node_config.service_listen_endpoint->host == "127.0.0.1");
    XS_CHECK(node_config.service_listen_endpoint->port == 7100);
    XS_CHECK(node_config.managed.has_value());
    XS_CHECK(node_config.managed->assembly_name == "XServer.Managed.GameLogic");
    XS_CHECK(node_config.logging.root_dir == "logs/game");
    XS_CHECK(node_config.logging.min_level == xs::core::LogLevel::Info);

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigRejectsUnknownTopLevelField() {
    const std::filesystem::path base_path = PrepareTestDirectory("config-unknown-top-level");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    config_json["unexpected"] = xs::core::Json::object();
    if (!WriteJsonFile(file_path, config_json)) {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::NodeConfig node_config;
    std::string error_message;
    const bool success = xs::core::LoadNodeConfigFile(file_path, "gm", &node_config, &error_message);

    XS_CHECK(!success);
    XS_CHECK_MSG(error_message.find("unexpected") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigRejectsUnknownLoggingField() {
    const std::filesystem::path base_path = PrepareTestDirectory("config-unknown-logging");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    config_json["logging"]["unexpectedField"] = 1;
    if (!WriteJsonFile(file_path, config_json)) {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::NodeConfig node_config;
    std::string error_message;
    const bool success = xs::core::LoadNodeConfigFile(file_path, "gm", &node_config, &error_message);

    XS_CHECK(!success);
    XS_CHECK_MSG(error_message.find("unexpectedField") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

void TestLoadNodeConfigRejectsMismatchedNodeId() {
    const std::filesystem::path base_path = PrepareTestDirectory("config-mismatched-node-id");
    const std::filesystem::path file_path = base_path / "config.json";
    xs::core::Json config_json = MakeValidClusterConfigJson();
    config_json["gate"]["gate0"]["nodeId"] = "Gate7";
    if (!WriteJsonFile(file_path, config_json)) {
        CleanupTestDirectory(base_path);
        return;
    }

    xs::core::NodeConfig node_config;
    std::string error_message;
    const bool success = xs::core::LoadNodeConfigFile(file_path, "gate0", &node_config, &error_message);

    XS_CHECK(!success);
    XS_CHECK_MSG(error_message.find("Gate0") != std::string::npos, error_message.c_str());

    CleanupTestDirectory(base_path);
}

} // namespace

int main() {
    TestTryParseJsonSuccess();
    TestTryParseJsonFailure();
    TestDeserializeTypedConfig();
    TestSaveAndLoadJsonFileRoundTrip();
    TestSaveJsonFileRejectsInvalidIndent();
    TestParseNodeSelector();
    TestLoadNodeConfigForGm();
    TestLoadNodeConfigForGate();
    TestLoadNodeConfigForGame();
    TestLoadNodeConfigRejectsUnknownTopLevelField();
    TestLoadNodeConfigRejectsUnknownLoggingField();
    TestLoadNodeConfigRejectsMismatchedNodeId();

    if (g_failures != 0) {
        std::cerr << g_failures << " JSON runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

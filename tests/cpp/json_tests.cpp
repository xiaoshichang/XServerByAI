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
    const std::filesystem::path base_path =
        std::filesystem::current_path() / "test-output" / "json-runtime-tests";
    const std::filesystem::path file_path = base_path / "config.json";

    std::error_code error_code;
    std::filesystem::remove_all(base_path, error_code);

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

    std::filesystem::remove_all(base_path, error_code);
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

} // namespace

int main() {
    TestTryParseJsonSuccess();
    TestTryParseJsonFailure();
    TestDeserializeTypedConfig();
    TestSaveAndLoadJsonFileRoundTrip();
    TestSaveJsonFileRejectsInvalidIndent();

    if (g_failures != 0) {
        std::cerr << g_failures << " JSON runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

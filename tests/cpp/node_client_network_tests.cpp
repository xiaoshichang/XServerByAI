#include "ClientNetwork.h"

#include "ClientSession.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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

xs::core::LoggerOptions MakeLoggerOptions(const std::filesystem::path& root_dir)
{
    xs::core::LoggerOptions options;
    options.process_type = xs::core::ProcessType::Gate;
    options.instance_id = "Gate0";
    options.config.root_dir = root_dir.string();
    options.config.min_level = xs::core::LogLevel::Info;
    return options;
}

xs::net::Endpoint MakeEndpoint(std::string host, std::uint16_t port)
{
    return xs::net::Endpoint{
        .host = std::move(host),
        .port = port,
    };
}

void TestClientNetworkCreatesIndexesAndRemovesSessions()
{
    const std::filesystem::path base_path = PrepareTestDirectory("node-client-network");
    const std::filesystem::path log_dir = base_path / "logs";

    xs::core::Logger logger(MakeLoggerOptions(log_dir));
    xs::core::MainEventLoop event_loop({.thread_name = "node-client-network-tests"});

    xs::node::ClientNetworkOptions options;
    options.owner_node_id = "Gate0";
    options.listen_endpoint = "0.0.0.0:4000";

    xs::node::ClientNetwork network(event_loop, logger, options);
    XS_CHECK_MSG(network.Init() == xs::node::NodeErrorCode::None, network.last_error_message().data());
    XS_CHECK(network.initialized());
    XS_CHECK(network.session_count() == 0U);

    std::uint64_t session_id_1 = 0U;
    XS_CHECK(
        network.CreateSession(11U, MakeEndpoint("127.0.0.1", 50000U), &session_id_1, 100U) ==
        xs::node::NodeErrorCode::None);
    XS_CHECK(session_id_1 == 1U);
    XS_CHECK(network.session_count() == 1U);

    const xs::node::ClientSession* first_session = network.FindSession(session_id_1);
    XS_CHECK(first_session != nullptr);
    if (first_session != nullptr)
    {
        const xs::node::ClientSessionSnapshot snapshot = first_session->snapshot();
        XS_CHECK(snapshot.gate_node_id == "Gate0");
        XS_CHECK(snapshot.conversation == 11U);
        XS_CHECK(snapshot.connected_at_unix_ms == 100U);
    }

    std::uint64_t session_id_2 = 0U;
    XS_CHECK(
        network.CreateSession(12U, MakeEndpoint("127.0.0.1", 50001U), &session_id_2, 200U) ==
        xs::node::NodeErrorCode::None);
    XS_CHECK(session_id_2 == 2U);
    XS_CHECK(network.session_count() == 2U);

    const xs::node::ClientSession* second_by_conversation = network.FindSessionByConversation(12U);
    XS_CHECK(second_by_conversation != nullptr);
    if (second_by_conversation != nullptr)
    {
        XS_CHECK(second_by_conversation->session_id() == session_id_2);
    }

    XS_CHECK(
        network.CreateSession(12U, MakeEndpoint("127.0.0.1", 50002U), nullptr, 300U) ==
        xs::node::NodeErrorCode::InvalidArgument);
    XS_CHECK(std::string(network.last_error_message()).find("conversation") != std::string::npos);

    XS_CHECK(network.Run() == xs::node::NodeErrorCode::None);
    XS_CHECK(network.running());
    XS_CHECK(network.Stop() == xs::node::NodeErrorCode::None);
    XS_CHECK(!network.running());

    XS_CHECK(network.RemoveSession(session_id_1));
    XS_CHECK(network.session_count() == 1U);
    XS_CHECK(network.FindSession(session_id_1) == nullptr);
    XS_CHECK(!network.RemoveSession(session_id_1));

    XS_CHECK(network.Uninit() == xs::node::NodeErrorCode::None);
    XS_CHECK(!network.initialized());

    logger.Flush();
    XS_CHECK(DirectoryContainsRegularFile(log_dir));

    const std::string log_text = ReadDirectoryText(log_dir);
    XS_CHECK(log_text.find("Client network initialized.") != std::string::npos);
    XS_CHECK(log_text.find("Client session created.") != std::string::npos);
    XS_CHECK(log_text.find("Client session removed.") != std::string::npos);

    CleanupTestDirectory(base_path);
}

} // namespace

int main()
{
    TestClientNetworkCreatesIndexesAndRemovesSessions();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " client network test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

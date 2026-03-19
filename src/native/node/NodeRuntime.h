#pragma once

#include "Config.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace xs::node
{

struct NodeCommandLineArgs
{
    std::filesystem::path config_path{};
    std::string selector{};
};

struct NodeRuntimeContext
{
    xs::core::ProcessType process_type{xs::core::ProcessType::Gm};
    std::string selector{"gm"};
    std::string node_id{"GM"};
    std::filesystem::path config_path{};
    std::uint32_t pid{0};
    xs::core::NodeConfig node_config{};
};

enum class NodeRuntimeErrorCode : std::uint8_t
{
    None = 0,
    InvalidArgument,
    InvalidArgumentCount,
    EmptyConfigPath,
    EmptySelector,
    InvalidSelector,
    ConfigLoadFailed,
    LoggerInitFailed,
    NodeCreateFailed,
    NodeInitFailed,
    NodeRunFailed,
    EventLoopFailed,
    UnsupportedProcessType,
};

class ServerNode;
using ServerNodePtr = std::unique_ptr<ServerNode>;
using ServerNodeFactory = std::function<ServerNodePtr(
    const NodeRuntimeContext& context,
    xs::core::Logger& logger,
    xs::core::MainEventLoop& event_loop,
    std::string* error_message)>;

struct NodeRuntimeRunOptions
{
    std::optional<xs::core::MainEventLoopOptions> event_loop_options{};
};

[[nodiscard]] std::string_view NodeUsage() noexcept;
[[nodiscard]] std::string_view NodeRuntimeErrorMessage(NodeRuntimeErrorCode code) noexcept;
[[nodiscard]] NodeRuntimeErrorCode ParseNodeCommandLine(
    int argc,
    char* argv[],
    NodeCommandLineArgs* output,
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode LoadNodeRuntimeContext(
    const NodeCommandLineArgs& args,
    NodeRuntimeContext* output,
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode CreateServerNode(
    const NodeRuntimeContext& context,
    xs::core::Logger& logger,
    xs::core::MainEventLoop& event_loop,
    ServerNodePtr* output,
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode RunNodeProcess(
    const NodeRuntimeContext& context,
    ServerNodeFactory factory,
    NodeRuntimeRunOptions options = {},
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode RunNodeProcess(
    const NodeRuntimeContext& context,
    NodeRuntimeRunOptions options = {},
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode RunNodeProcess(
    const NodeCommandLineArgs& args,
    ServerNodeFactory factory,
    NodeRuntimeRunOptions options = {},
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode RunNodeProcess(
    const NodeCommandLineArgs& args,
    NodeRuntimeRunOptions options = {},
    std::string* error_message = nullptr);

} // namespace xs::node

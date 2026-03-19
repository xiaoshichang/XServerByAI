#pragma once

#include "Config.h"
#include "Logging.h"
#include "MainEventLoop.h"

#include <cstdint>
#include <filesystem>
#include <functional>
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
    MissingRoleRunner,
    LoggerInitFailed,
    RoleRunnerFailed,
    EventLoopFailed,
    UnsupportedProcessType,
};

using NodeRuntimeStopCallback = std::function<void(xs::core::MainEventLoop& event_loop)>;

struct NodeRoleRuntimeBindings
{
    NodeRuntimeStopCallback on_stop{};
};

using NodeRoleRunner = std::function<NodeRuntimeErrorCode(
    const NodeRuntimeContext& context,
    xs::core::Logger& logger,
    xs::core::MainEventLoop& event_loop,
    NodeRoleRuntimeBindings* runtime_bindings,
    std::string* error_message)>;

struct NodeRoleRunners
{
    NodeRoleRunner gm{};
    NodeRoleRunner gate{};
    NodeRoleRunner game{};
};

struct NodeRuntimeRunOptions
{
    std::optional<xs::core::MainEventLoopOptions> event_loop_options{};
};

[[nodiscard]] std::string_view NodeUsage() noexcept;
[[nodiscard]] std::string_view NodeRuntimeErrorMessage(NodeRuntimeErrorCode code) noexcept;
[[nodiscard]] NodeRoleRunners DefaultNodeRoleRunners();
[[nodiscard]] NodeRuntimeErrorCode ParseNodeCommandLine(
    int argc,
    char* argv[],
    NodeCommandLineArgs* output,
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode LoadNodeRuntimeContext(
    const NodeCommandLineArgs& args,
    NodeRuntimeContext* output,
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode RunNodeProcess(
    const NodeRuntimeContext& context,
    const NodeRoleRunners& role_runners,
    NodeRuntimeRunOptions options = {},
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode RunNodeProcess(
    const NodeRuntimeContext& context,
    NodeRuntimeRunOptions options = {},
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode RunNodeProcess(
    const NodeCommandLineArgs& args,
    const NodeRoleRunners& role_runners,
    NodeRuntimeRunOptions options = {},
    std::string* error_message = nullptr);
[[nodiscard]] NodeRuntimeErrorCode RunNodeProcess(
    const NodeCommandLineArgs& args,
    NodeRuntimeRunOptions options = {},
    std::string* error_message = nullptr);

} // namespace xs::node

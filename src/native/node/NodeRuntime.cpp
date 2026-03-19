#include "NodeRuntime.h"

#include "NodeGameRunner.h"
#include "NodeGateRunner.h"
#include "NodeGmRunner.h"

#include <exception>
#include <memory>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace xs::node
{
namespace
{

void ClearError(std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

NodeRuntimeErrorCode SetError(
    NodeRuntimeErrorCode code,
    std::string message,
    std::string* error_message)
{
    if (error_message != nullptr)
    {
        if (message.empty())
        {
            *error_message = std::string(NodeRuntimeErrorMessage(code));
        }
        else
        {
            *error_message = std::move(message);
        }
    }

    return code;
}

std::uint32_t GetCurrentProcessIdValue() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

std::string BuildDefaultThreadName(std::string_view selector)
{
    if (selector.empty())
    {
        return "xs-node";
    }

    return "xs-" + std::string(selector);
}

xs::core::LoggerOptions BuildLoggerOptions(const NodeRuntimeContext& context)
{
    xs::core::LoggerOptions options;
    options.process_type = context.process_type;
    options.instance_id = context.node_id;
    options.config = context.node_config.logging;
    return options;
}

xs::core::MainEventLoopOptions ResolveEventLoopOptions(
    const NodeRuntimeContext& context,
    const NodeRuntimeRunOptions& options)
{
    if (options.event_loop_options.has_value())
    {
        return *options.event_loop_options;
    }

    xs::core::MainEventLoopOptions resolved_options;
    resolved_options.thread_name = BuildDefaultThreadName(context.selector);
    return resolved_options;
}

NodeRuntimeErrorCode SelectRoleRunner(
    const NodeRuntimeContext& context,
    const NodeRoleRunners& role_runners,
    const NodeRoleRunner** output,
    std::string* error_message)
{
    if (output == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Node role runner output must not be null.",
            error_message);
    }

    switch (context.process_type)
    {
    case xs::core::ProcessType::Gm:
        if (!role_runners.gm)
        {
            return SetError(
                NodeRuntimeErrorCode::MissingRoleRunner,
                "GM role runner is not configured.",
                error_message);
        }

        *output = &role_runners.gm;
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;

    case xs::core::ProcessType::Gate:
        if (!role_runners.gate)
        {
            return SetError(
                NodeRuntimeErrorCode::MissingRoleRunner,
                "Gate role runner is not configured.",
                error_message);
        }

        *output = &role_runners.gate;
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;

    case xs::core::ProcessType::Game:
        if (!role_runners.game)
        {
            return SetError(
                NodeRuntimeErrorCode::MissingRoleRunner,
                "Game role runner is not configured.",
                error_message);
        }

        *output = &role_runners.game;
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;
    }

    return SetError(
        NodeRuntimeErrorCode::UnsupportedProcessType,
        "Node runtime does not support the selected process type.",
        error_message);
}

NodeRuntimeErrorCode MapConfigError(xs::core::ConfigErrorCode code) noexcept
{
    if (code == xs::core::ConfigErrorCode::InvalidSelector)
    {
        return NodeRuntimeErrorCode::InvalidSelector;
    }

    return NodeRuntimeErrorCode::ConfigLoadFailed;
}

} // namespace

std::string_view NodeUsage() noexcept
{
    return "Usage: xserver-node <configPath> <gm|gateN|gameN>";
}

std::string_view NodeRuntimeErrorMessage(NodeRuntimeErrorCode code) noexcept
{
    switch (code)
    {
    case NodeRuntimeErrorCode::None:
        return "No error.";
    case NodeRuntimeErrorCode::InvalidArgument:
        return "Invalid node runtime argument.";
    case NodeRuntimeErrorCode::InvalidArgumentCount:
        return "xserver-node requires exactly 2 arguments.";
    case NodeRuntimeErrorCode::EmptyConfigPath:
        return "configPath must not be empty.";
    case NodeRuntimeErrorCode::EmptySelector:
        return "selector must not be empty.";
    case NodeRuntimeErrorCode::InvalidSelector:
        return "Node selector is invalid.";
    case NodeRuntimeErrorCode::ConfigLoadFailed:
        return "Failed to load node configuration.";
    case NodeRuntimeErrorCode::MissingRoleRunner:
        return "No role runner is registered for the selected process type.";
    case NodeRuntimeErrorCode::LoggerInitFailed:
        return "Failed to initialize node logger.";
    case NodeRuntimeErrorCode::RoleRunnerFailed:
        return "Node role runner failed.";
    case NodeRuntimeErrorCode::EventLoopFailed:
        return "Node event loop failed.";
    case NodeRuntimeErrorCode::UnsupportedProcessType:
        return "Node runtime does not support the selected process type.";
    }

    return "Unknown node runtime error.";
}

NodeRoleRunners DefaultNodeRoleRunners()
{
    NodeRoleRunners role_runners;
    role_runners.gm = RunGmNode;
    role_runners.gate = RunGateNode;
    role_runners.game = RunGameNode;
    return role_runners;
}

NodeRuntimeErrorCode ParseNodeCommandLine(
    int argc,
    char* argv[],
    NodeCommandLineArgs* output,
    std::string* error_message)
{
    if (output == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Node command line output must not be null.",
            error_message);
    }

    if (argc != 3)
    {
        return SetError(NodeRuntimeErrorCode::InvalidArgumentCount, std::string(NodeUsage()), error_message);
    }

    if (argv == nullptr || argv[1] == nullptr || argv[2] == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Node command line arguments must not be null.",
            error_message);
    }

    NodeCommandLineArgs args;
    args.config_path = argv[1];
    args.selector = argv[2];

    if (args.config_path.empty())
    {
        return SetError(NodeRuntimeErrorCode::EmptyConfigPath, "configPath must not be empty.", error_message);
    }

    if (args.selector.empty())
    {
        return SetError(NodeRuntimeErrorCode::EmptySelector, "selector must not be empty.", error_message);
    }

    *output = std::move(args);
    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

NodeRuntimeErrorCode LoadNodeRuntimeContext(
    const NodeCommandLineArgs& args,
    NodeRuntimeContext* output,
    std::string* error_message)
{
    if (output == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Node runtime context output must not be null.",
            error_message);
    }

    if (args.config_path.empty())
    {
        return SetError(NodeRuntimeErrorCode::EmptyConfigPath, "configPath must not be empty.", error_message);
    }

    if (args.selector.empty())
    {
        return SetError(NodeRuntimeErrorCode::EmptySelector, "selector must not be empty.", error_message);
    }

    xs::core::NodeConfig node_config;
    std::string detail_error;
    const xs::core::ConfigErrorCode load_result =
        xs::core::LoadNodeConfigFile(args.config_path, args.selector, &node_config, &detail_error);
    if (load_result != xs::core::ConfigErrorCode::None)
    {
        if (detail_error.empty())
        {
            detail_error = std::string(xs::core::ConfigErrorMessage(load_result));
        }

        return SetError(MapConfigError(load_result), std::move(detail_error), error_message);
    }

    NodeRuntimeContext context;
    context.process_type = node_config.process_type;
    context.selector = node_config.selector;
    context.node_id = node_config.instance_id;
    context.config_path = args.config_path;
    context.pid = GetCurrentProcessIdValue();
    context.node_config = std::move(node_config);

    *output = std::move(context);
    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

NodeRuntimeErrorCode RunNodeProcess(
    const NodeRuntimeContext& context,
    const NodeRoleRunners& role_runners,
    NodeRuntimeRunOptions options,
    std::string* error_message)
{
    const NodeRoleRunner* role_runner = nullptr;
    const NodeRuntimeErrorCode select_result = SelectRoleRunner(context, role_runners, &role_runner, error_message);
    if (select_result != NodeRuntimeErrorCode::None)
    {
        return select_result;
    }

    std::unique_ptr<xs::core::Logger> logger;
    try
    {
        logger = std::make_unique<xs::core::Logger>(BuildLoggerOptions(context));
    }
    catch (const std::exception& exception)
    {
        return SetError(
            NodeRuntimeErrorCode::LoggerInitFailed,
            std::string("Failed to initialize node logger: ") + exception.what(),
            error_message);
    }
    catch (...)
    {
        return SetError(
            NodeRuntimeErrorCode::LoggerInitFailed,
            "Failed to initialize node logger: unknown exception.",
            error_message);
    }

    xs::core::MainEventLoop event_loop(ResolveEventLoopOptions(context, options));

    NodeRuntimeErrorCode runner_result = NodeRuntimeErrorCode::None;
    std::string runner_error;
    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [&context, &logger, role_runner, &runner_result, &runner_error](
                         xs::core::MainEventLoop& started_event_loop,
                         std::string* loop_error) {
        try
        {
            runner_result = (*role_runner)(context, *logger, started_event_loop, &runner_error);
        }
        catch (const std::exception& exception)
        {
            runner_result = NodeRuntimeErrorCode::RoleRunnerFailed;
            runner_error = std::string("Role runner threw: ") + exception.what();
        }
        catch (...)
        {
            runner_result = NodeRuntimeErrorCode::RoleRunnerFailed;
            runner_error = "Role runner threw an unknown exception.";
        }

        if (runner_result != NodeRuntimeErrorCode::None)
        {
            if (runner_error.empty())
            {
                runner_error = std::string(NodeRuntimeErrorMessage(runner_result));
            }

            if (loop_error != nullptr)
            {
                *loop_error = runner_error;
            }

            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        return xs::core::MainEventLoopErrorCode::None;
    };

    std::string loop_error;
    const xs::core::MainEventLoopErrorCode loop_result = event_loop.Run(std::move(hooks), &loop_error);
    if (runner_result != NodeRuntimeErrorCode::None)
    {
        return SetError(NodeRuntimeErrorCode::RoleRunnerFailed, std::move(runner_error), error_message);
    }

    if (loop_result != xs::core::MainEventLoopErrorCode::None)
    {
        if (loop_error.empty())
        {
            loop_error = std::string(xs::core::MainEventLoopErrorMessage(loop_result));
        }

        return SetError(NodeRuntimeErrorCode::EventLoopFailed, std::move(loop_error), error_message);
    }

    ClearError(error_message);
    return NodeRuntimeErrorCode::None;
}

NodeRuntimeErrorCode RunNodeProcess(
    const NodeRuntimeContext& context,
    NodeRuntimeRunOptions options,
    std::string* error_message)
{
    return RunNodeProcess(context, DefaultNodeRoleRunners(), std::move(options), error_message);
}

NodeRuntimeErrorCode RunNodeProcess(
    const NodeCommandLineArgs& args,
    const NodeRoleRunners& role_runners,
    NodeRuntimeRunOptions options,
    std::string* error_message)
{
    NodeRuntimeContext context;
    const NodeRuntimeErrorCode load_result = LoadNodeRuntimeContext(args, &context, error_message);
    if (load_result != NodeRuntimeErrorCode::None)
    {
        return load_result;
    }

    return RunNodeProcess(context, role_runners, std::move(options), error_message);
}

NodeRuntimeErrorCode RunNodeProcess(
    const NodeCommandLineArgs& args,
    NodeRuntimeRunOptions options,
    std::string* error_message)
{
    return RunNodeProcess(args, DefaultNodeRoleRunners(), std::move(options), error_message);
}

} // namespace xs::node
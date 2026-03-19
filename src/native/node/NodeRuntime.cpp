#include "NodeRuntime.h"

#include "GameNode.h"
#include "GateNode.h"
#include "GmNode.h"
#include "ServerNode.h"

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

std::string BuildStageFailureMessage(
    NodeRuntimeErrorCode stage_code,
    NodeRuntimeErrorCode operation_code)
{
    std::string message = std::string(NodeRuntimeErrorMessage(stage_code));
    if (operation_code == NodeRuntimeErrorCode::None || operation_code == stage_code)
    {
        return message;
    }

    message += " Cause: ";
    message += NodeRuntimeErrorMessage(operation_code);
    return message;
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

NodeRuntimeErrorCode MapConfigError(xs::core::ConfigErrorCode code) noexcept
{
    if (code == xs::core::ConfigErrorCode::InvalidSelector)
    {
        return NodeRuntimeErrorCode::InvalidSelector;
    }

    return NodeRuntimeErrorCode::ConfigLoadFailed;
}

ServerNodeFactory DefaultServerNodeFactory()
{
    return [](
               const NodeRuntimeContext& context,
               xs::core::Logger& logger,
               xs::core::MainEventLoop& event_loop,
               std::string* error_message) -> ServerNodePtr {
        ServerNodePtr node;
        const NodeRuntimeErrorCode result = CreateServerNode(context, logger, event_loop, &node, error_message);
        if (result != NodeRuntimeErrorCode::None)
        {
            return nullptr;
        }

        return node;
    };
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
    case NodeRuntimeErrorCode::LoggerInitFailed:
        return "Failed to initialize node logger.";
    case NodeRuntimeErrorCode::NodeCreateFailed:
        return "Failed to create server node.";
    case NodeRuntimeErrorCode::NodeInitFailed:
        return "Server node initialization failed.";
    case NodeRuntimeErrorCode::NodeRunFailed:
        return "Server node run failed.";
    case NodeRuntimeErrorCode::EventLoopFailed:
        return "Node event loop failed.";
    case NodeRuntimeErrorCode::UnsupportedProcessType:
        return "Node runtime does not support the selected process type.";
    }

    return "Unknown node runtime error.";
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

NodeRuntimeErrorCode CreateServerNode(
    const NodeRuntimeContext& context,
    xs::core::Logger& logger,
    xs::core::MainEventLoop& event_loop,
    ServerNodePtr* output,
    std::string* error_message)
{
    if (output == nullptr)
    {
        return SetError(
            NodeRuntimeErrorCode::InvalidArgument,
            "Server node output must not be null.",
            error_message);
    }

    ServerNodeEnvironment environment{
        .context = context,
        .logger = logger,
        .event_loop = event_loop,
    };

    switch (context.process_type)
    {
    case xs::core::ProcessType::Gm:
        *output = std::make_unique<GmNode>(environment);
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;

    case xs::core::ProcessType::Gate:
        *output = std::make_unique<GateNode>(environment);
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;

    case xs::core::ProcessType::Game:
        *output = std::make_unique<GameNode>(environment);
        ClearError(error_message);
        return NodeRuntimeErrorCode::None;
    }

    return SetError(
        NodeRuntimeErrorCode::UnsupportedProcessType,
        "Node runtime does not support the selected process type.",
        error_message);
}

NodeRuntimeErrorCode RunNodeProcess(
    const NodeRuntimeContext& context,
    ServerNodeFactory factory,
    NodeRuntimeRunOptions options,
    std::string* error_message)
{
    if (!factory)
    {
        return SetError(
            NodeRuntimeErrorCode::NodeCreateFailed,
            "Server node factory is not configured.",
            error_message);
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

    ServerNodePtr server_node;
    NodeRuntimeErrorCode node_result = NodeRuntimeErrorCode::None;
    std::string node_error;
    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [&context, &logger, &factory, &server_node, &node_result, &node_error](
                         xs::core::MainEventLoop& started_event_loop,
                         std::string* loop_error) {
        try
        {
            server_node = factory(context, *logger, started_event_loop, &node_error);
        }
        catch (const std::exception& exception)
        {
            node_result = NodeRuntimeErrorCode::NodeCreateFailed;
            node_error = std::string("Server node factory threw: ") + exception.what();
        }
        catch (...)
        {
            node_result = NodeRuntimeErrorCode::NodeCreateFailed;
            node_error = "Server node factory threw an unknown exception.";
        }

        if (node_result != NodeRuntimeErrorCode::None)
        {
            if (loop_error != nullptr)
            {
                *loop_error = node_error;
            }

            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        if (server_node == nullptr)
        {
            node_result = NodeRuntimeErrorCode::NodeCreateFailed;
            if (node_error.empty())
            {
                node_error = "Server node factory returned null.";
            }

            if (loop_error != nullptr)
            {
                *loop_error = node_error;
            }

            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        try
        {
            node_error.clear();
            const NodeRuntimeErrorCode init_result = server_node->Init(&node_error);
            if (init_result != NodeRuntimeErrorCode::None)
            {
                node_result = NodeRuntimeErrorCode::NodeInitFailed;
                if (node_error.empty())
                {
                    node_error = BuildStageFailureMessage(node_result, init_result);
                }
            }
        }
        catch (const std::exception& exception)
        {
            node_result = NodeRuntimeErrorCode::NodeInitFailed;
            node_error = std::string("Server node Init() threw: ") + exception.what();
        }
        catch (...)
        {
            node_result = NodeRuntimeErrorCode::NodeInitFailed;
            node_error = "Server node Init() threw an unknown exception.";
        }

        if (node_result != NodeRuntimeErrorCode::None)
        {
            if (node_error.empty())
            {
                node_error = std::string(NodeRuntimeErrorMessage(node_result));
            }

            if (loop_error != nullptr)
            {
                *loop_error = node_error;
            }

            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        try
        {
            node_error.clear();
            const NodeRuntimeErrorCode run_result = server_node->Run(&node_error);
            if (run_result != NodeRuntimeErrorCode::None)
            {
                node_result = NodeRuntimeErrorCode::NodeRunFailed;
                if (node_error.empty())
                {
                    node_error = BuildStageFailureMessage(node_result, run_result);
                }
            }
        }
        catch (const std::exception& exception)
        {
            node_result = NodeRuntimeErrorCode::NodeRunFailed;
            node_error = std::string("Server node Run() threw: ") + exception.what();
        }
        catch (...)
        {
            node_result = NodeRuntimeErrorCode::NodeRunFailed;
            node_error = "Server node Run() threw an unknown exception.";
        }

        if (node_result != NodeRuntimeErrorCode::None)
        {
            if (node_error.empty())
            {
                node_error = std::string(NodeRuntimeErrorMessage(node_result));
            }

            if (loop_error != nullptr)
            {
                *loop_error = node_error;
            }

            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        return xs::core::MainEventLoopErrorCode::None;
    };
    hooks.on_stop = [&server_node](xs::core::MainEventLoop&) {
        if (server_node != nullptr)
        {
            server_node->Uninit();
        }
    };

    std::string loop_error;
    const xs::core::MainEventLoopErrorCode loop_result = event_loop.Run(std::move(hooks), &loop_error);
    if (node_result != NodeRuntimeErrorCode::None)
    {
        return SetError(node_result, std::move(node_error), error_message);
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
    return RunNodeProcess(context, DefaultServerNodeFactory(), std::move(options), error_message);
}

NodeRuntimeErrorCode RunNodeProcess(
    const NodeCommandLineArgs& args,
    ServerNodeFactory factory,
    NodeRuntimeRunOptions options,
    std::string* error_message)
{
    NodeRuntimeContext context;
    const NodeRuntimeErrorCode load_result = LoadNodeRuntimeContext(args, &context, error_message);
    if (load_result != NodeRuntimeErrorCode::None)
    {
        return load_result;
    }

    return RunNodeProcess(context, std::move(factory), std::move(options), error_message);
}

NodeRuntimeErrorCode RunNodeProcess(
    const NodeCommandLineArgs& args,
    NodeRuntimeRunOptions options,
    std::string* error_message)
{
    return RunNodeProcess(args, DefaultServerNodeFactory(), std::move(options), error_message);
}

} // namespace xs::node

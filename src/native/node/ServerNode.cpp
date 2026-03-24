#include "ServerNode.h"

#include "InnerNetwork.h"

#include <asio/post.hpp>

#include <exception>
#include <optional>
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

std::uint32_t GetCurrentProcessIdValue() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

std::string BuildDefaultThreadName(std::string_view node_id)
{
    if (node_id.empty())
    {
        return "xs-node";
    }

    return "xs-" + std::string(node_id);
}

xs::core::LoggerOptions BuildLoggerOptions(
    const xs::core::ClusterConfig& cluster_config,
    xs::core::ProcessType process_type,
    std::string_view node_id)
{
    xs::core::LoggerOptions options;
    options.process_type = process_type;
    options.instance_id = std::string(node_id);
    options.config = cluster_config.logging;
    return options;
}

std::optional<xs::core::ProcessType> ResolveNodeProcessType(const xs::core::NodeConfig& node_config) noexcept
{
    if (dynamic_cast<const xs::core::GmNodeConfig*>(&node_config) != nullptr)
    {
        return xs::core::ProcessType::Gm;
    }

    if (dynamic_cast<const xs::core::GateNodeConfig*>(&node_config) != nullptr)
    {
        return xs::core::ProcessType::Gate;
    }

    if (dynamic_cast<const xs::core::GameNodeConfig*>(&node_config) != nullptr)
    {
        return xs::core::ProcessType::Game;
    }

    return std::nullopt;
}

NodeErrorCode MapConfigError(xs::core::ConfigErrorCode code) noexcept
{
    if (code == xs::core::ConfigErrorCode::InvalidNodeId)
    {
        return NodeErrorCode::InvalidNodeId;
    }

    return NodeErrorCode::ConfigLoadFailed;
}

std::string BuildProcessTypeMismatchMessage(xs::core::ProcessType expected)
{
    const std::string process_type_name = std::string(xs::core::ProcessTypeName(expected));
    return process_type_name + " node requires nodeId resolving to " + process_type_name + ".";
}

} // namespace

ServerNode::ServerNode(NodeCommandLineArgs args)
    : args_(std::move(args))
{
}

ServerNode::~ServerNode() = default;

NodeErrorCode ServerNode::Init()
{
    if (initialized_)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Server node is already initialized.");
    }

    if (args_.config_path.empty())
    {
        return SetError(NodeErrorCode::EmptyConfigPath, "configPath must not be empty.");
    }

    if (args_.node_id.empty())
    {
        return SetError(NodeErrorCode::EmptyNodeId, "nodeId must not be empty.");
    }

    node_config_.reset();
    cluster_config_ = xs::core::ClusterConfig{};

    std::string detail_error;
    const xs::core::ConfigErrorCode load_cluster_result =
        xs::core::LoadClusterConfigFile(args_.config_path, &cluster_config_, &detail_error);
    if (load_cluster_result != xs::core::ConfigErrorCode::None)
    {
        if (detail_error.empty())
        {
            detail_error = std::string(NodeErrorMessage(MapConfigError(load_cluster_result)));
        }

        return SetError(MapConfigError(load_cluster_result), std::move(detail_error));
    }

    const xs::core::ConfigErrorCode select_result =
        xs::core::SelectNodeConfig(cluster_config_, args_.node_id, &node_config_, &detail_error);
    if (select_result != xs::core::ConfigErrorCode::None)
    {
        if (detail_error.empty())
        {
            detail_error = std::string(NodeErrorMessage(MapConfigError(select_result)));
        }

        return SetError(MapConfigError(select_result), std::move(detail_error));
    }

    const std::optional<xs::core::ProcessType> resolved_process_type = ResolveNodeProcessType(*node_config_);
    if (!resolved_process_type.has_value())
    {
        ReleaseCoreState();
        return SetError(NodeErrorCode::ConfigLoadFailed, "Selected node config type is unsupported.");
    }

    process_type_ = *resolved_process_type;
    node_id_ = args_.node_id;
    pid_ = GetCurrentProcessIdValue();

    const xs::core::ProcessType expected_process_type = role_process_type();
    if (process_type_ != expected_process_type)
    {
        ReleaseCoreState();
        return SetError(NodeErrorCode::InvalidArgument, BuildProcessTypeMismatchMessage(expected_process_type));
    }

    try
    {
        logger_ = std::make_unique<xs::core::Logger>(BuildLoggerOptions(cluster_config_, process_type_, node_id_));
    }
    catch (const std::exception& exception)
    {
        ReleaseCoreState();
        return SetError(
            NodeErrorCode::LoggerInitFailed,
            std::string("Failed to initialize node logger: ") + exception.what());
    }
    catch (...)
    {
        ReleaseCoreState();
        return SetError(NodeErrorCode::LoggerInitFailed, "Failed to initialize node logger: unknown exception.");
    }

    try
    {
        xs::core::MainEventLoopOptions options;
        options.thread_name = BuildDefaultThreadName(node_id());
        event_loop_ = std::make_unique<xs::core::MainEventLoop>(std::move(options));
    }
    catch (const std::exception& exception)
    {
        ReleaseCoreState();
        return SetError(
            NodeErrorCode::NodeInitFailed,
            std::string("Failed to initialize node event loop: ") + exception.what());
    }
    catch (...)
    {
        ReleaseCoreState();
        return SetError(NodeErrorCode::NodeInitFailed, "Failed to initialize node event loop: unknown exception.");
    }

    try
    {
        const NodeErrorCode init_result = OnInit();
        if (init_result != NodeErrorCode::None)
        {
            std::string init_error_message =
                last_error_message_.empty() ? std::string(NodeErrorMessage(init_result)) : last_error_message_;

            try
            {
                (void)OnUninit();
            }
            catch (...)
            {
            }

            ReleaseCoreState();
            last_error_message_ = std::move(init_error_message);
            return init_result;
        }
    }
    catch (const std::exception& exception)
    {
        try
        {
            (void)OnUninit();
        }
        catch (...)
        {
        }

        ReleaseCoreState();
        return SetError(
            NodeErrorCode::NodeInitFailed,
            std::string("Server node Init() threw: ") + exception.what());
    }
    catch (...)
    {
        try
        {
            (void)OnUninit();
        }
        catch (...)
        {
        }

        ReleaseCoreState();
        return SetError(NodeErrorCode::NodeInitFailed, "Server node Init() threw an unknown exception.");
    }

    initialized_ = true;
    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode ServerNode::Run()
{
    if (!initialized_ || event_loop_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Server node must be initialized before Run().");
    }

    NodeErrorCode run_result = NodeErrorCode::None;
    std::string run_error_message;
    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [this, &run_result, &run_error_message](xs::core::MainEventLoop&, std::string* loop_error) {
        try
        {
            run_result = OnRun();
        }
        catch (const std::exception& exception)
        {
            run_result = SetError(
                NodeErrorCode::NodeRunFailed,
                std::string("Server node Run() threw: ") + exception.what());
        }
        catch (...)
        {
            run_result = SetError(NodeErrorCode::NodeRunFailed, "Server node Run() threw an unknown exception.");
        }

        if (run_result != NodeErrorCode::None)
        {
            run_error_message =
                last_error_message_.empty() ? std::string(NodeErrorMessage(run_result)) : last_error_message_;
            if (loop_error != nullptr)
            {
                *loop_error = run_error_message;
            }

            return xs::core::MainEventLoopErrorCode::StartupCallbackFailed;
        }

        return xs::core::MainEventLoopErrorCode::None;
    };

    std::string loop_error;
    const xs::core::MainEventLoopErrorCode loop_result = event_loop_->Run(std::move(hooks), &loop_error);
    if (run_result != NodeErrorCode::None)
    {
        return SetError(run_result, std::move(run_error_message));
    }

    if (loop_result != xs::core::MainEventLoopErrorCode::None)
    {
        if (loop_error.empty())
        {
            loop_error = std::string(xs::core::MainEventLoopErrorMessage(loop_result));
        }

        return SetError(NodeErrorCode::EventLoopFailed, std::move(loop_error));
    }

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode ServerNode::Uninit()
{
    if (!initialized_)
    {
        ClearError();
        return NodeErrorCode::None;
    }

    NodeErrorCode result = NodeErrorCode::None;
    try
    {
        result = OnUninit();
    }
    catch (const std::exception& exception)
    {
        result = SetError(
            NodeErrorCode::NodeUninitFailed,
            std::string("Server node Uninit() threw: ") + exception.what());
    }
    catch (...)
    {
        result = SetError(NodeErrorCode::NodeUninitFailed, "Server node Uninit() threw an unknown exception.");
    }

    ReleaseCoreState();
    if (result == NodeErrorCode::None)
    {
        ClearError();
    }
    else if (last_error_message_.empty())
    {
        last_error_message_ = std::string(NodeErrorMessage(result));
    }

    return result;
}

void ServerNode::RequestStop() noexcept
{
    if (event_loop_ != nullptr)
    {
        try
        {
            xs::core::MainEventLoop* loop = event_loop_.get();
            asio::post(event_loop_->executor(), [loop]() {
                if (loop != nullptr)
                {
                    loop->RequestStop();
                }
            });
        }
        catch (...)
        {
            try
            {
                event_loop_->RequestStop();
            }
            catch (...)
            {
            }
        }
    }
}

const std::filesystem::path& ServerNode::config_path() const noexcept
{
    return args_.config_path;
}

xs::core::ProcessType ServerNode::process_type() const noexcept
{
    return process_type_;
}

std::string_view ServerNode::node_id() const noexcept
{
    return node_id_;
}

std::uint32_t ServerNode::pid() const noexcept
{
    return pid_;
}

const xs::core::NodeConfig& ServerNode::node_config() const noexcept
{
    return *node_config_;
}

bool ServerNode::initialized() const noexcept
{
    return initialized_;
}

std::string_view ServerNode::last_error_message() const noexcept
{
    return last_error_message_;
}

const xs::core::ClusterConfig& ServerNode::cluster_config() const noexcept
{
    return cluster_config_;
}

xs::core::Logger& ServerNode::logger() const noexcept
{
    return *logger_;
}

xs::core::MainEventLoop& ServerNode::event_loop() const noexcept
{
    return *event_loop_;
}

InnerNetwork* ServerNode::inner_network() noexcept
{
    return inner_network_.get();
}

const InnerNetwork* ServerNode::inner_network() const noexcept
{
    return inner_network_.get();
}

InnerNetworkSessionManager& ServerNode::inner_network_remote_sessions() noexcept
{
    return inner_network_remote_sessions_;
}

const InnerNetworkSessionManager& ServerNode::inner_network_remote_sessions() const noexcept
{
    return inner_network_remote_sessions_;
}

NodeErrorCode ServerNode::InitInnerNetwork(InnerNetworkOptions options)
{
    if (inner_network_ != nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Inner network is already initialized.");
    }

    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), std::move(options));
    const NodeErrorCode init_result = inner_network_->Init();
    if (init_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        return SetError(init_result, error_message);
    }

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode ServerNode::RunInnerNetwork()
{
    if (inner_network_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Inner network must be initialized before Run().");
    }

    const NodeErrorCode run_result = inner_network_->Run();
    if (run_result != NodeErrorCode::None)
    {
        return SetError(run_result, std::string(inner_network_->last_error_message()));
    }

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode ServerNode::UninitInnerNetwork()
{
    if (inner_network_ == nullptr)
    {
        ClearError();
        return NodeErrorCode::None;
    }

    const NodeErrorCode result = inner_network_->Uninit();
    const std::string error_message = std::string(inner_network_->last_error_message());
    inner_network_.reset();
    if (result != NodeErrorCode::None)
    {
        return SetError(result, error_message);
    }

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode ServerNode::SetError(NodeErrorCode code, std::string message)
{
    if (message.empty())
    {
        last_error_message_ = std::string(NodeErrorMessage(code));
    }
    else
    {
        last_error_message_ = std::move(message);
    }

    return code;
}

void ServerNode::ClearError() noexcept
{
    last_error_message_.clear();
}

void ServerNode::ReleaseCoreState() noexcept
{
    inner_network_.reset();
    inner_network_remote_sessions_.Clear();

    if (logger_ != nullptr)
    {
        try
        {
            logger_->Flush();
        }
        catch (...)
        {
        }
    }

    event_loop_.reset();
    logger_.reset();
    node_config_.reset();
    cluster_config_ = xs::core::ClusterConfig{};
    process_type_ = xs::core::ProcessType::Gm;
    node_id_.clear();
    pid_ = 0U;
    initialized_ = false;
}

} // namespace xs::node

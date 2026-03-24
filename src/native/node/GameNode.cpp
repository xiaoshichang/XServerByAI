#include "GameNode.h"

#include "TimeUtils.h"
#include "message/PacketCodec.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

constexpr std::string_view kGameBuildVersion = "dev";

std::string BuildTcpEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return "tcp://" + endpoint.host + ":" + std::to_string(endpoint.port);
}

std::string ToString(std::uint64_t value)
{
    return std::to_string(value);
}

std::uint64_t CurrentUnixTimeMillisecondsValue() noexcept
{
    const std::int64_t now_unix_ms = xs::core::ToUnixTimeMilliseconds(xs::core::UtcNow());
    return now_unix_ms > 0 ? static_cast<std::uint64_t>(now_unix_ms) : 0U;
}

} // namespace

GameNode::GameNode(NodeCommandLineArgs args)
    : ServerNode(std::move(args))
{
}

GameNode::~GameNode() = default;

std::string_view GameNode::gm_inner_remote_endpoint() const noexcept
{
    return gm_inner_remote_endpoint_;
}

std::string_view GameNode::configured_inner_endpoint() const noexcept
{
    return configured_inner_endpoint_;
}

std::string_view GameNode::managed_assembly_name() const noexcept
{
    return runtime_state_.managed_assembly_name;
}

ipc::ZmqConnectionState GameNode::gm_inner_connection_state() const noexcept
{
    return inner_network_ != nullptr ? inner_network_->connection_state() : ipc::ZmqConnectionState::Stopped;
}

xs::core::ProcessType GameNode::role_process_type() const noexcept
{
    return xs::core::ProcessType::Game;
}

NodeErrorCode GameNode::OnInit()
{
    const auto* config = dynamic_cast<const xs::core::GameNodeConfig*>(&node_config());
    if (config == nullptr)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Game node requires GameNodeConfig.");
    }

    const xs::core::EndpointConfig& gm_endpoint = cluster_config().gm.inner_network_listen_endpoint;
    if (gm_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM innerNetwork.listenEndpoint.host must not be empty.");
    }

    if (gm_endpoint.port == 0U)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "GM innerNetwork.listenEndpoint.port must be greater than zero.");
    }

    const xs::core::EndpointConfig& inner_endpoint = config->inner_network_listen_endpoint;
    if (inner_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Game innerNetwork.listenEndpoint.host must not be empty.");
    }

    if (inner_endpoint.port == 0U)
    {
        return SetError(
            NodeErrorCode::ConfigLoadFailed,
            "Game innerNetwork.listenEndpoint.port must be greater than zero.");
    }

    gm_inner_remote_endpoint_ = BuildTcpEndpoint(gm_endpoint);
    configured_inner_endpoint_ = BuildTcpEndpoint(inner_endpoint);
    configured_inner_endpoint_config_ = inner_endpoint;
    gm_inner_connection_state_cache_ = ipc::ZmqConnectionState::Stopped;
    runtime_state_ = RuntimeState{};
    runtime_state_.build_version = std::string(kGameBuildVersion);
    runtime_state_.managed_assembly_name = config->managed.assembly_name;
    runtime_state_.started_at_unix_ms = CurrentUnixTimeMillisecondsValue();

    InnerNetworkOptions inner_options;
    inner_options.mode = InnerNetworkMode::ActiveConnector;
    inner_options.local_endpoint = configured_inner_endpoint_;
    inner_options.remote_endpoint = gm_inner_remote_endpoint_;

    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), std::move(inner_options));
    inner_network_->SetConnectionStateHandler([this](ipc::ZmqConnectionState state) {
        HandleInnerConnectionStateChanged(state);
    });
    inner_network_->SetMessageHandler([this](std::vector<std::byte>, std::vector<std::byte> payload) {
        HandleInnerMessage(payload);
    });

    const NodeErrorCode init_result = inner_network_->Init();
    if (init_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        gm_inner_remote_endpoint_.clear();
        configured_inner_endpoint_.clear();
        configured_inner_endpoint_config_ = xs::core::EndpointConfig{};
        gm_inner_connection_state_cache_ = ipc::ZmqConnectionState::Stopped;
        ResetRuntimeState();
        return SetError(init_result, error_message);
    }

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", runtime_state_.build_version},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "Game node configured runtime skeleton.", context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GameNode::OnRun()
{
    if (inner_network_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Game node must be initialized before Run().");
    }

    const NodeErrorCode inner_result = inner_network_->Run();
    if (inner_result != NodeErrorCode::None)
    {
        return SetError(inner_result, std::string(inner_network_->last_error_message()));
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{
            "gmInnerState",
            std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state())),
        },
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "Game node entered runtime state.", context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GameNode::OnUninit()
{
    if (inner_network_ != nullptr)
    {
        const NodeErrorCode result = inner_network_->Uninit();
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        gm_inner_remote_endpoint_.clear();
        configured_inner_endpoint_.clear();
        configured_inner_endpoint_config_ = xs::core::EndpointConfig{};
        gm_inner_connection_state_cache_ = ipc::ZmqConnectionState::Stopped;
        ResetRuntimeState();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }
    else
    {
        gm_inner_remote_endpoint_.clear();
        configured_inner_endpoint_.clear();
        configured_inner_endpoint_config_ = xs::core::EndpointConfig{};
        gm_inner_connection_state_cache_ = ipc::ZmqConnectionState::Stopped;
        ResetRuntimeState();
    }

    ClearError();
    return NodeErrorCode::None;
}

void GameNode::HandleInnerConnectionStateChanged(ipc::ZmqConnectionState state)
{
    gm_inner_connection_state_cache_ = state;

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerState", std::string(ipc::ZmqConnectionStateName(state))},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node observed GM inner connection state change.", context);
}

void GameNode::HandleInnerMessage(std::span<const std::byte> payload)
{
    xs::net::PacketView packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::DecodePacket(payload, &packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        runtime_state_.last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));

        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(payload.size()))},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
            xs::core::LogContextField{
                "gmInnerState",
                std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state_cache_)),
            },
            xs::core::LogContextField{"packetError", runtime_state_.last_protocol_error},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node ignored malformed GM inner packet.", context);
        return;
    }

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
        xs::core::LogContextField{"payloadBytes", ToString(static_cast<std::uint64_t>(packet.payload.size()))},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored unsupported GM inner packet.", context);
}

void GameNode::ResetRuntimeState() noexcept
{
    runtime_state_ = RuntimeState{};
}

} // namespace xs::node

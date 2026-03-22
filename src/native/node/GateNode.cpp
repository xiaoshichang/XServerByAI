#include "GateNode.h"

#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace xs::node
{
namespace
{

std::string BuildEndpointText(const xs::core::EndpointConfig& endpoint)
{
    std::ostringstream stream;
    stream << endpoint.host << ':' << endpoint.port;
    return stream.str();
}

std::string BuildTcpEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return "tcp://" + endpoint.host + ":" + std::to_string(endpoint.port);
}

} // namespace

GateNode::GateNode(NodeCommandLineArgs args)
    : ServerNode(std::move(args))
{
}

GateNode::~GateNode() = default;

std::string_view GateNode::gm_inner_remote_endpoint() const noexcept
{
    return gm_inner_remote_endpoint_;
}

std::string_view GateNode::configured_inner_endpoint() const noexcept
{
    return configured_inner_endpoint_;
}

ipc::ZmqConnectionState GateNode::gm_inner_connection_state() const noexcept
{
    return inner_network_ != nullptr ? inner_network_->connection_state() : ipc::ZmqConnectionState::Stopped;
}

xs::core::ProcessType GateNode::role_process_type() const noexcept
{
    return xs::core::ProcessType::Gate;
}

NodeErrorCode GateNode::OnInit()
{
    const auto* config = dynamic_cast<const xs::core::GateNodeConfig*>(&node_config());
    if (config == nullptr)
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Gate node requires GateNodeConfig.");
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
        return SetError(NodeErrorCode::ConfigLoadFailed, "Gate innerNetwork.listenEndpoint.host must not be empty.");
    }

    if (inner_endpoint.port == 0U)
    {
        return SetError(
            NodeErrorCode::ConfigLoadFailed,
            "Gate innerNetwork.listenEndpoint.port must be greater than zero.");
    }

    const xs::core::EndpointConfig& client_endpoint = config->client_network_listen_endpoint;
    if (client_endpoint.host.empty())
    {
        return SetError(NodeErrorCode::ConfigLoadFailed, "Gate clientNetwork.listenEndpoint.host must not be empty.");
    }

    if (client_endpoint.port == 0U)
    {
        return SetError(
            NodeErrorCode::ConfigLoadFailed,
            "Gate clientNetwork.listenEndpoint.port must be greater than zero.");
    }

    gm_inner_remote_endpoint_ = BuildTcpEndpoint(gm_endpoint);
    configured_inner_endpoint_ = BuildTcpEndpoint(inner_endpoint);

    InnerNetworkOptions inner_options;
    inner_options.mode = InnerNetworkMode::ActiveConnector;
    inner_options.local_endpoint = configured_inner_endpoint_;
    inner_options.remote_endpoint = gm_inner_remote_endpoint_;

    inner_network_ = std::make_unique<InnerNetwork>(event_loop(), logger(), std::move(inner_options));
    inner_network_->SetMessageHandler([this](std::vector<std::byte>, std::vector<std::byte> payload) {
        HandleInnerMessage(payload);
    });

    ClientNetworkOptions client_options;
    client_options.listen_endpoint = BuildEndpointText(client_endpoint);
    client_options.kcp = cluster_config().kcp;
    client_network_ = std::make_unique<ClientNetwork>(event_loop(), logger(), std::move(client_options));

    const NodeErrorCode inner_result = inner_network_->Init();
    if (inner_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        client_network_.reset();
        return SetError(inner_result, error_message);
    }

    const NodeErrorCode client_result = client_network_->Init();
    if (client_result != NodeErrorCode::None)
    {
        const std::string error_message = std::string(client_network_->last_error_message());
        (void)inner_network_->Uninit();
        inner_network_.reset();
        client_network_.reset();
        return SetError(client_result, error_message);
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"clientListenEndpoint", std::string(client_network_->configured_endpoint())},
        xs::core::LogContextField{"kcpMtu", std::to_string(cluster_config().kcp.mtu)},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "Gate node configured runtime skeleton.", context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GateNode::OnRun()
{
    if (inner_network_ == nullptr || client_network_ == nullptr)
    {
        return SetError(NodeErrorCode::InvalidArgument, "Gate node must be initialized before Run().");
    }

    const NodeErrorCode inner_result = inner_network_->Run();
    if (inner_result != NodeErrorCode::None)
    {
        return SetError(inner_result, std::string(inner_network_->last_error_message()));
    }

    const NodeErrorCode client_result = client_network_->Run();
    if (client_result != NodeErrorCode::None)
    {
        return SetError(client_result, std::string(client_network_->last_error_message()));
    }

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{
            "gmInnerState",
            std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state())),
        },
        xs::core::LogContextField{"clientListenEndpoint", std::string(client_network_->configured_endpoint())},
    };
    logger().Log(xs::core::LogLevel::Info, "runtime", "Gate node entered runtime state.", context);

    ClearError();
    return NodeErrorCode::None;
}

NodeErrorCode GateNode::OnUninit()
{
    if (client_network_ != nullptr)
    {
        const NodeErrorCode result = client_network_->Uninit();
        const std::string error_message = std::string(client_network_->last_error_message());
        client_network_.reset();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }

    if (inner_network_ != nullptr)
    {
        const NodeErrorCode result = inner_network_->Uninit();
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        if (result != NodeErrorCode::None)
        {
            return SetError(result, error_message);
        }
    }

    ClearError();
    return NodeErrorCode::None;
}

void GateNode::HandleInnerMessage(std::span<const std::byte> payload)
{
    const std::array<xs::core::LogContextField, 3> context{
        xs::core::LogContextField{"payloadBytes", std::to_string(payload.size())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{
            "gmInnerState",
            std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state())),
        },
    };
    logger().Log(
        xs::core::LogLevel::Info,
        "inner",
        "Gate node ignored GM inner payload because no protocol handler is installed yet.",
        context);
}

} // namespace xs::node

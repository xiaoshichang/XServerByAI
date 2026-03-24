#include "GameNode.h"

#include "TimeUtils.h"
#include "message/HeartbeatCodec.h"
#include "message/PacketCodec.h"
#include "message/RegisterCodec.h"

#include <array>
#include <chrono>
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
constexpr std::uint16_t kResponseFlags = static_cast<std::uint16_t>(xs::net::PacketFlag::Response);
constexpr std::uint16_t kErrorResponseFlags =
    kResponseFlags | static_cast<std::uint16_t>(xs::net::PacketFlag::Error);

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

xs::net::Endpoint ToNetEndpoint(const xs::core::EndpointConfig& endpoint)
{
    return xs::net::Endpoint{
        .host = endpoint.host,
        .port = endpoint.port,
    };
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
    runtime_state_.started_at_unix_ms = CurrentUnixTimeMilliseconds();
    gm_session_state_ = GmSessionState{};

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
        ResetGmSessionState();
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
    ResetGmSessionState();
    gm_inner_connection_state_cache_ = ipc::ZmqConnectionState::Stopped;

    if (inner_network_ != nullptr)
    {
        const NodeErrorCode result = inner_network_->Uninit();
        const std::string error_message = std::string(inner_network_->last_error_message());
        inner_network_.reset();
        gm_inner_remote_endpoint_.clear();
        configured_inner_endpoint_.clear();
        configured_inner_endpoint_config_ = xs::core::EndpointConfig{};
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
        ResetRuntimeState();
    }

    ClearError();
    return NodeErrorCode::None;
}

void GameNode::HandleInnerConnectionStateChanged(ipc::ZmqConnectionState state)
{
    gm_inner_connection_state_cache_ = state;

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerState", std::string(ipc::ZmqConnectionStateName(state))},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        xs::core::LogContextField{"registered", gm_session_state_.registered ? "true" : "false"},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node observed GM inner connection state change.", context);

    if (state != ipc::ZmqConnectionState::Connected)
    {
        ResetGmSessionState();
        return;
    }

    if (!gm_session_state_.registered && !gm_session_state_.register_in_flight)
    {
        (void)SendRegisterRequest();
    }
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

    if ((packet.header.flags != kResponseFlags && packet.header.flags != kErrorResponseFlags) ||
        packet.header.seq == xs::net::kPacketSeqNone)
    {
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{
                "gmInnerState",
                std::string(ipc::ZmqConnectionStateName(gm_inner_connection_state_cache_)),
            },
        };
        runtime_state_.last_protocol_error = "GM response envelope is invalid.";
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node ignored GM response with an invalid envelope.", context);
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerRegisterMsgId)
    {
        HandleRegisterResponse(packet);
        return;
    }

    if (packet.header.msg_id == xs::net::kInnerHeartbeatMsgId)
    {
        HandleHeartbeatResponse(packet);
        return;
    }

    const std::array<xs::core::LogContextField, 4> context{
        xs::core::LogContextField{"msgId", std::to_string(packet.header.msg_id)},
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored an unsupported GM response packet.", context);
}

void GameNode::HandleRegisterResponse(const xs::net::PacketView& packet)
{
    if (gm_session_state_.register_seq == 0U || packet.header.seq != gm_session_state_.register_seq)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(gm_session_state_.register_seq)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM register response.", context);
        return;
    }

    gm_session_state_.register_in_flight = false;
    gm_session_state_.register_seq = 0U;

    if (packet.header.flags == kErrorResponseFlags)
    {
        xs::net::RegisterErrorResponse response{};
        const xs::net::RegisterCodecErrorCode decode_result =
            xs::net::DecodeRegisterErrorResponse(packet.payload, &response);
        if (decode_result != xs::net::RegisterCodecErrorCode::None)
        {
            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
                xs::core::LogContextField{
                    "codecError",
                    std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
                },
                xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            };
            gm_session_state_.registered = false;
            runtime_state_.last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM register error response.", context);
            return;
        }

        gm_session_state_.registered = false;
        runtime_state_.last_protocol_error =
            "GM rejected register request with error code " + std::to_string(response.error_code) + ".";

        const std::array<xs::core::LogContextField, 7> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"errorCode", std::to_string(response.error_code)},
            xs::core::LogContextField{"retryAfterMs", std::to_string(response.retry_after_ms)},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node received GM register error response.",
            context);
        return;
    }

    xs::net::RegisterSuccessResponse response{};
    const xs::net::RegisterCodecErrorCode decode_result =
        xs::net::DecodeRegisterSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::RegisterCodecErrorCode::None)
    {
        gm_session_state_.registered = false;
        runtime_state_.last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(decode_result));

        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::RegisterCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM register success response.", context);
        return;
    }

    gm_session_state_.registered = true;
    gm_session_state_.heartbeat_interval_ms = response.heartbeat_interval_ms;
    gm_session_state_.heartbeat_timeout_ms = response.heartbeat_timeout_ms;
    gm_session_state_.last_server_now_unix_ms = response.server_now_unix_ms;
    gm_session_state_.heartbeat_seq = 0U;
    runtime_state_.last_protocol_error.clear();

    StartOrResetHeartbeatTimer(response.heartbeat_interval_ms);

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM register success response.", context);
}

void GameNode::HandleHeartbeatResponse(const xs::net::PacketView& packet)
{
    if (gm_session_state_.heartbeat_seq == 0U || packet.header.seq != gm_session_state_.heartbeat_seq)
    {
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"expectedSeq", std::to_string(gm_session_state_.heartbeat_seq)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Info, "inner", "Game node ignored a stale GM heartbeat response.", context);
        return;
    }

    gm_session_state_.heartbeat_seq = 0U;

    if (packet.header.flags == kErrorResponseFlags)
    {
        xs::net::HeartbeatErrorResponse response{};
        const xs::net::HeartbeatCodecErrorCode decode_result =
            xs::net::DecodeHeartbeatErrorResponse(packet.payload, &response);
        if (decode_result != xs::net::HeartbeatCodecErrorCode::None)
        {
            const std::array<xs::core::LogContextField, 4> context{
                xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
                xs::core::LogContextField{"flags", std::to_string(packet.header.flags)},
                xs::core::LogContextField{
                    "codecError",
                    std::string(xs::net::HeartbeatCodecErrorMessage(decode_result)),
                },
                xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            };
            runtime_state_.last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
            logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM heartbeat error response.", context);
            return;
        }

        runtime_state_.last_protocol_error =
            "GM rejected heartbeat with error code " + std::to_string(response.error_code) + ".";

        const std::array<xs::core::LogContextField, 7> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{"errorCode", std::to_string(response.error_code)},
            xs::core::LogContextField{"retryAfterMs", std::to_string(response.retry_after_ms)},
            xs::core::LogContextField{"requireFullRegister", response.require_full_register ? "true" : "false"},
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(
            xs::core::LogLevel::Warn,
            "inner",
            "Game node received GM heartbeat error response.",
            context);

        if (response.require_full_register)
        {
            ResetGmSessionState();
            if (gm_inner_connection_state_cache_ == ipc::ZmqConnectionState::Connected)
            {
                (void)SendRegisterRequest();
            }
        }

        return;
    }

    xs::net::HeartbeatSuccessResponse response{};
    const xs::net::HeartbeatCodecErrorCode decode_result =
        xs::net::DecodeHeartbeatSuccessResponse(packet.payload, &response);
    if (decode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
            xs::core::LogContextField{
                "codecError",
                std::string(xs::net::HeartbeatCodecErrorMessage(decode_result)),
            },
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        runtime_state_.last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(decode_result));
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to decode GM heartbeat success response.", context);
        return;
    }

    const bool heartbeat_config_changed =
        gm_session_state_.heartbeat_interval_ms != response.heartbeat_interval_ms ||
        gm_session_state_.heartbeat_timeout_ms != response.heartbeat_timeout_ms ||
        gm_session_state_.heartbeat_timer_id == 0;

    gm_session_state_.heartbeat_interval_ms = response.heartbeat_interval_ms;
    gm_session_state_.heartbeat_timeout_ms = response.heartbeat_timeout_ms;
    gm_session_state_.last_server_now_unix_ms = response.server_now_unix_ms;
    runtime_state_.last_protocol_error.clear();

    if (heartbeat_config_changed)
    {
        StartOrResetHeartbeatTimer(response.heartbeat_interval_ms);
    }

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"seq", std::to_string(packet.header.seq)},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(response.heartbeat_interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(response.heartbeat_timeout_ms)},
        xs::core::LogContextField{"serverNowUnixMs", ToString(response.server_now_unix_ms)},
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node accepted GM heartbeat success response.", context);
}

bool GameNode::SendRegisterRequest()
{
    if (inner_network_ == nullptr || gm_inner_connection_state_cache_ != ipc::ZmqConnectionState::Connected ||
        gm_session_state_.register_in_flight)
    {
        return false;
    }

    const xs::net::RegisterRequest request{
        .process_type = static_cast<std::uint16_t>(xs::net::InnerProcessType::Game),
        .process_flags = 0U,
        .node_id = std::string(node_id()),
        .pid = pid(),
        .started_at_unix_ms = runtime_state_.started_at_unix_ms,
        .inner_network_endpoint = ToNetEndpoint(configured_inner_endpoint_config_),
        .build_version = runtime_state_.build_version,
        .capability_tags = gm_session_state_.capability_tags,
        .load = xs::net::LoadSnapshot{},
    };

    std::size_t payload_size = 0U;
    const xs::net::RegisterCodecErrorCode size_result =
        xs::net::GetRegisterRequestWireSize(request, &payload_size);
    if (size_result != xs::net::RegisterCodecErrorCode::None)
    {
        runtime_state_.last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(size_result));
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(size_result))},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to size GM register request.", context);
        return false;
    }

    std::vector<std::byte> payload(payload_size);
    const xs::net::RegisterCodecErrorCode encode_result = xs::net::EncodeRegisterRequest(request, payload);
    if (encode_result != xs::net::RegisterCodecErrorCode::None)
    {
        runtime_state_.last_protocol_error = std::string(xs::net::RegisterCodecErrorMessage(encode_result));
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"codecError", std::string(xs::net::RegisterCodecErrorMessage(encode_result))},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode GM register request.", context);
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence();
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerRegisterMsgId,
        seq,
        0U,
        static_cast<std::uint32_t>(payload.size()));

    std::vector<std::byte> packet(xs::net::kPacketHeaderSize + payload.size());
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        runtime_state_.last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap GM register request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network_->Send({}, packet);
    if (send_result != NodeErrorCode::None)
    {
        runtime_state_.last_protocol_error = std::string(inner_network_->last_error_message());
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send GM register request.", context);
        return false;
    }

    gm_session_state_.register_in_flight = true;
    gm_session_state_.register_seq = seq;
    runtime_state_.last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 7> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"startedAtUnixMs", ToString(runtime_state_.started_at_unix_ms)},
        xs::core::LogContextField{"buildVersion", runtime_state_.build_version},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent GM register request.", context);
    return true;
}

bool GameNode::SendHeartbeatRequest()
{
    if (inner_network_ == nullptr || !gm_session_state_.registered ||
        gm_inner_connection_state_cache_ != ipc::ZmqConnectionState::Connected ||
        gm_session_state_.heartbeat_seq != 0U)
    {
        return false;
    }

    const xs::net::HeartbeatRequest request{
        .sent_at_unix_ms = CurrentUnixTimeMilliseconds(),
        .status_flags = 0U,
        .load = xs::net::LoadSnapshot{},
    };

    std::array<std::byte, xs::net::kHeartbeatRequestSize> payload{};
    const xs::net::HeartbeatCodecErrorCode encode_result = xs::net::EncodeHeartbeatRequest(request, payload);
    if (encode_result != xs::net::HeartbeatCodecErrorCode::None)
    {
        runtime_state_.last_protocol_error = std::string(xs::net::HeartbeatCodecErrorMessage(encode_result));
        const std::array<xs::core::LogContextField, 3> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"codecError", std::string(xs::net::HeartbeatCodecErrorMessage(encode_result))},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to encode GM heartbeat request.", context);
        return false;
    }

    const std::uint32_t seq = ConsumeNextInnerSequence();
    const xs::net::PacketHeader header = xs::net::MakePacketHeader(
        xs::net::kInnerHeartbeatMsgId,
        seq,
        0U,
        static_cast<std::uint32_t>(payload.size()));
    std::array<std::byte, xs::net::kPacketHeaderSize + xs::net::kHeartbeatRequestSize> packet{};
    const xs::net::PacketCodecErrorCode packet_result = xs::net::EncodePacket(header, payload, packet);
    if (packet_result != xs::net::PacketCodecErrorCode::None)
    {
        runtime_state_.last_protocol_error = std::string(xs::net::PacketCodecErrorMessage(packet_result));
        const std::array<xs::core::LogContextField, 4> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"packetError", std::string(xs::net::PacketCodecErrorMessage(packet_result))},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to wrap GM heartbeat request into a packet.", context);
        return false;
    }

    const NodeErrorCode send_result = inner_network_->Send({}, packet);
    if (send_result != NodeErrorCode::None)
    {
        runtime_state_.last_protocol_error = std::string(inner_network_->last_error_message());
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"seq", std::to_string(seq)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Warn, "inner", "Game node failed to send GM heartbeat request.", context);
        return false;
    }

    gm_session_state_.heartbeat_seq = seq;
    runtime_state_.last_protocol_error.clear();

    const std::array<xs::core::LogContextField, 6> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"seq", std::to_string(seq)},
        xs::core::LogContextField{"sentAtUnixMs", ToString(request.sent_at_unix_ms)},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"configuredInnerEndpoint", configured_inner_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node sent GM heartbeat request.", context);
    return true;
}

void GameNode::ResetRuntimeState() noexcept
{
    runtime_state_ = RuntimeState{};
}

void GameNode::ResetGmSessionState()
{
    CancelHeartbeatTimer();
    gm_session_state_.registered = false;
    gm_session_state_.register_in_flight = false;
    gm_session_state_.register_seq = 0U;
    gm_session_state_.heartbeat_seq = 0U;
    gm_session_state_.heartbeat_interval_ms = 0U;
    gm_session_state_.heartbeat_timeout_ms = 0U;
    gm_session_state_.last_server_now_unix_ms = 0U;
}

void GameNode::StartOrResetHeartbeatTimer(std::uint32_t interval_ms)
{
    CancelHeartbeatTimer();

    const xs::core::TimerCreateResult timer_result =
        event_loop().timers().CreateRepeating(std::chrono::milliseconds(interval_ms), [this]() {
            (void)SendHeartbeatRequest();
        });
    if (!xs::core::IsTimerID(timer_result))
    {
        runtime_state_.last_protocol_error =
            "Failed to create GM heartbeat timer: " +
            std::string(xs::core::TimerErrorMessage(xs::core::TimerErrorFromCreateResult(timer_result)));
        const std::array<xs::core::LogContextField, 5> context{
            xs::core::LogContextField{"nodeId", std::string(node_id())},
            xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
            xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(gm_session_state_.heartbeat_timeout_ms)},
            xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
            xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
        };
        logger().Log(xs::core::LogLevel::Error, "inner", "Game node failed to schedule GM heartbeat timer.", context);
        return;
    }

    gm_session_state_.heartbeat_timer_id = timer_result;

    const std::array<xs::core::LogContextField, 5> context{
        xs::core::LogContextField{"nodeId", std::string(node_id())},
        xs::core::LogContextField{"heartbeatIntervalMs", std::to_string(interval_ms)},
        xs::core::LogContextField{"heartbeatTimeoutMs", std::to_string(gm_session_state_.heartbeat_timeout_ms)},
        xs::core::LogContextField{"gmInnerRemoteEndpoint", gm_inner_remote_endpoint_},
        xs::core::LogContextField{"managedAssemblyName", runtime_state_.managed_assembly_name},
    };
    logger().Log(xs::core::LogLevel::Info, "inner", "Game node scheduled GM heartbeat timer.", context);
}

void GameNode::CancelHeartbeatTimer() noexcept
{
    if (gm_session_state_.heartbeat_timer_id > 0)
    {
        (void)event_loop().timers().Cancel(gm_session_state_.heartbeat_timer_id);
        gm_session_state_.heartbeat_timer_id = 0;
    }
}

std::uint32_t GameNode::ConsumeNextInnerSequence() noexcept
{
    std::uint32_t seq = gm_session_state_.next_seq;
    if (seq == xs::net::kPacketSeqNone)
    {
        seq = 1U;
    }

    gm_session_state_.next_seq = seq + 1U;
    if (gm_session_state_.next_seq == xs::net::kPacketSeqNone)
    {
        gm_session_state_.next_seq = 1U;
    }

    return seq;
}

std::uint64_t GameNode::CurrentUnixTimeMilliseconds() const noexcept
{
    return CurrentUnixTimeMillisecondsValue();
}

} // namespace xs::node

#pragma once

#include <cstdint>
#include <string_view>

namespace xs::ipc
{

enum class ZmqSocketErrorCode : std::uint8_t
{
    None = 0,
    InvalidState,
    AlreadyRunning,
    ContextNotInitialized,
    EndpointEmpty,
    PollIntervalMustBePositive,
    HighWaterMarkNegative,
    TimingOptionNegative,
    SocketCreateFailed,
    SocketOptionFailed,
    MonitorEnableFailed,
    MonitorSocketCreateFailed,
    MonitorSocketConnectFailed,
    ConnectFailed,
    BindFailed,
    LastEndpointQueryFailed,
    NotStarted,
    EmptyRoutingId,
    RoutingIdSendFailed,
    PayloadSendFailed,
    MessageSendFailed,
    ReceiveFailed,
    MonitorReceiveFailed,
    MultipartNotSupported,
    PayloadMissing,
    TimerPumpFailed,
};

[[nodiscard]] constexpr std::string_view ZmqSocketErrorMessage(ZmqSocketErrorCode code) noexcept
{
    switch (code)
    {
    case ZmqSocketErrorCode::None:
        return "No error.";
    case ZmqSocketErrorCode::InvalidState:
        return "Object is in an invalid state.";
    case ZmqSocketErrorCode::AlreadyRunning:
        return "Socket endpoint is already running.";
    case ZmqSocketErrorCode::ContextNotInitialized:
        return "ZeroMQ context is not initialized.";
    case ZmqSocketErrorCode::EndpointEmpty:
        return "ZeroMQ endpoint must not be empty.";
    case ZmqSocketErrorCode::PollIntervalMustBePositive:
        return "ZeroMQ poll interval must be greater than zero.";
    case ZmqSocketErrorCode::HighWaterMarkNegative:
        return "ZeroMQ high water marks must not be negative.";
    case ZmqSocketErrorCode::TimingOptionNegative:
        return "ZeroMQ timing options must not be negative.";
    case ZmqSocketErrorCode::SocketCreateFailed:
        return "Failed to create ZeroMQ socket.";
    case ZmqSocketErrorCode::SocketOptionFailed:
        return "Failed to configure ZeroMQ socket option.";
    case ZmqSocketErrorCode::MonitorEnableFailed:
        return "Failed to enable ZeroMQ socket monitor.";
    case ZmqSocketErrorCode::MonitorSocketCreateFailed:
        return "Failed to create ZeroMQ monitor socket.";
    case ZmqSocketErrorCode::MonitorSocketConnectFailed:
        return "Failed to connect ZeroMQ monitor socket.";
    case ZmqSocketErrorCode::ConnectFailed:
        return "Failed to connect ZeroMQ socket.";
    case ZmqSocketErrorCode::BindFailed:
        return "Failed to bind ZeroMQ socket.";
    case ZmqSocketErrorCode::LastEndpointQueryFailed:
        return "Failed to query ZeroMQ last endpoint.";
    case ZmqSocketErrorCode::NotStarted:
        return "ZeroMQ socket endpoint has not been started.";
    case ZmqSocketErrorCode::EmptyRoutingId:
        return "ZeroMQ routing id must not be empty.";
    case ZmqSocketErrorCode::RoutingIdSendFailed:
        return "Failed to send ZeroMQ routing id frame.";
    case ZmqSocketErrorCode::PayloadSendFailed:
        return "Failed to send ZeroMQ payload frame.";
    case ZmqSocketErrorCode::MessageSendFailed:
        return "Failed to send ZeroMQ message.";
    case ZmqSocketErrorCode::ReceiveFailed:
        return "Failed to receive ZeroMQ message.";
    case ZmqSocketErrorCode::MonitorReceiveFailed:
        return "Failed to receive ZeroMQ monitor event.";
    case ZmqSocketErrorCode::MultipartNotSupported:
        return "Multipart ZeroMQ messages are not supported.";
    case ZmqSocketErrorCode::PayloadMissing:
        return "ZeroMQ message payload frame is missing.";
    case ZmqSocketErrorCode::TimerPumpFailed:
        return "Asio timer pump failed while driving ZeroMQ.";
    }

    return "Unknown ZeroMQ error.";
}

} // namespace xs::ipc

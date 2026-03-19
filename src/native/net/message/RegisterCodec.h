#pragma once

#include "ControlMessageTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xs::net
{

inline constexpr std::uint32_t kControlRegisterMsgId = 1000u;
inline constexpr std::size_t kRegisterSuccessResponseSize = sizeof(std::uint32_t) + sizeof(std::uint32_t) + sizeof(std::uint64_t);
inline constexpr std::size_t kRegisterErrorResponseSize = sizeof(std::int32_t) + sizeof(std::uint32_t);
inline constexpr std::size_t kRegisterMaxCapabilityTagCount = 32u;

enum class RegisterCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidArgument = 3,
    InvalidProcessType = 4,
    InvalidProcessFlags = 5,
    InvalidNodeId = 6,
    InvalidServiceEndpointHost = 7,
    InvalidServiceEndpointPort = 8,
    InvalidHeartbeatTiming = 9,
    TooManyCapabilityTags = 10,
    TrailingBytes = 11,
};

[[nodiscard]] std::string_view RegisterCodecErrorMessage(RegisterCodecErrorCode error_code) noexcept;

struct RegisterRequest
{
    std::uint16_t process_type{0};
    std::uint16_t process_flags{0};
    std::string node_id{};
    std::uint32_t pid{0};
    std::uint64_t started_at_unix_ms{0};
    Endpoint service_endpoint{};
    std::string build_version{};
    std::vector<std::string> capability_tags{};
    LoadSnapshot load{};
};

struct RegisterSuccessResponse
{
    std::uint32_t heartbeat_interval_ms{0};
    std::uint32_t heartbeat_timeout_ms{0};
    std::uint64_t server_now_unix_ms{0};
};

struct RegisterErrorResponse
{
    std::int32_t error_code{0};
    std::uint32_t retry_after_ms{0};
};

[[nodiscard]] RegisterCodecErrorCode GetRegisterRequestWireSize(
    const RegisterRequest& message,
    std::size_t* wire_size) noexcept;
[[nodiscard]] RegisterCodecErrorCode EncodeRegisterRequest(
    const RegisterRequest& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] RegisterCodecErrorCode DecodeRegisterRequest(
    std::span<const std::byte> buffer,
    RegisterRequest* message) noexcept;

[[nodiscard]] RegisterCodecErrorCode EncodeRegisterSuccessResponse(
    const RegisterSuccessResponse& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] RegisterCodecErrorCode DecodeRegisterSuccessResponse(
    std::span<const std::byte> buffer,
    RegisterSuccessResponse* message) noexcept;

[[nodiscard]] RegisterCodecErrorCode EncodeRegisterErrorResponse(
    const RegisterErrorResponse& message,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] RegisterCodecErrorCode DecodeRegisterErrorResponse(
    std::span<const std::byte> buffer,
    RegisterErrorResponse* message) noexcept;

} // namespace xs::net


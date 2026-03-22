#pragma once

#include "Logging.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace xs::core
{

enum class ConfigErrorCode : std::uint8_t
{
    None = 0,
    InvalidArgument,
    JsonLoadFailed,
    ExpectedObject,
    UnknownField,
    MissingRequiredField,
    InvalidString,
    EmptyString,
    InvalidBoolean,
    InvalidUnsignedInteger,
    ValueOutOfRange,
    InvalidLogLevel,
    InvalidEndpointPort,
    InvalidNodeId,
    MissingInstance,
    EmptyCollection,
    LoggingConfigInvalid,
    Unknown,
};

struct EndpointConfig
{
    std::string host;
    std::uint16_t port{};
};

struct EnvConfig
{
    std::string id;
    std::string environment;
};

struct KcpConfig
{
    std::uint32_t mtu{1200};
    std::uint32_t sndwnd{128};
    std::uint32_t rcvwnd{128};
    bool nodelay{true};
    std::uint32_t interval_ms{10};
    std::uint32_t fast_resend{2};
    bool no_congestion_window{false};
    std::uint32_t min_rto_ms{30};
    std::uint32_t dead_link_count{20};
    bool stream_mode{false};
};

struct ManagedConfig
{
    std::string assembly_name{"XServer.Managed.GameLogic"};
};

struct GmConfig
{
    EndpointConfig inner_network_listen_endpoint;
    EndpointConfig control_network_listen_endpoint;
};

struct GateConfig
{
    EndpointConfig inner_network_listen_endpoint;
    EndpointConfig client_network_listen_endpoint;
};

struct GameConfig
{
    EndpointConfig inner_network_listen_endpoint;
    ManagedConfig managed{};
};

struct ClusterConfig
{
    EnvConfig env;
    LoggingConfig logging{};
    KcpConfig kcp{};
    GmConfig gm{};
    std::map<std::string, GateConfig, std::less<>> gates;
    std::map<std::string, GameConfig, std::less<>> games;
};

struct NodeConfig
{
    virtual ~NodeConfig() = default;
};

struct GmNodeConfig final : NodeConfig
{
    EndpointConfig inner_network_listen_endpoint{};
    EndpointConfig control_network_listen_endpoint{};
};

struct GateNodeConfig final : NodeConfig
{
    EndpointConfig inner_network_listen_endpoint{};
    EndpointConfig client_network_listen_endpoint{};
};

struct GameNodeConfig final : NodeConfig
{
    EndpointConfig inner_network_listen_endpoint{};
    ManagedConfig managed{};
};

[[nodiscard]] ConfigErrorCode LoadClusterConfigFile(
    const std::filesystem::path& path,
    ClusterConfig* output,
    std::string* error_message = nullptr);
[[nodiscard]] ConfigErrorCode SelectNodeConfig(
    const ClusterConfig& cluster_config,
    std::string_view node_id,
    std::unique_ptr<NodeConfig>* output,
    std::string* error_message = nullptr);

} // namespace xs::core

#pragma once

#include "Logging.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace xs::core
{

enum class NodeSelectorKind : std::uint8_t
{
    Gm,
    Gate,
    Game,
};

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
    InvalidSelector,
    MissingInstance,
    InvalidNodeId,
    EmptyCollection,
    LoggingConfigInvalid,
    Unknown,
};

struct NodeSelector
{
    NodeSelectorKind kind{NodeSelectorKind::Gm};
    std::string value{"gm"};
};

struct EndpointConfig
{
    std::string host;
    std::uint16_t port{};
};

struct ServerGroupConfig
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
    EndpointConfig control_listen_endpoint;
    LoggingConfig logging{};
};

struct GateConfig
{
    std::string selector;
    std::string node_id;
    EndpointConfig service_listen_endpoint;
    KcpConfig kcp{};
    LoggingConfig logging{};
};

struct GameConfig
{
    std::string selector;
    std::string node_id;
    EndpointConfig service_listen_endpoint;
    ManagedConfig managed{};
    LoggingConfig logging{};
};

struct ClusterConfig
{
    ServerGroupConfig server_group;
    LoggingConfig logging_defaults{};
    GmConfig gm{};
    std::map<std::string, GateConfig, std::less<>> gates;
    std::map<std::string, GameConfig, std::less<>> games;
};

struct NodeConfig
{
    ProcessType process_type{ProcessType::Gm};
    std::string selector{"gm"};
    std::string instance_id{"GM"};
    std::filesystem::path source_path;
    ServerGroupConfig server_group{};
    LoggingConfig logging{};
    std::optional<EndpointConfig> control_listen_endpoint;
    std::optional<EndpointConfig> service_listen_endpoint;
    std::optional<KcpConfig> kcp;
    std::optional<ManagedConfig> managed;
};

[[nodiscard]] std::optional<NodeSelector> ParseNodeSelector(std::string_view selector) noexcept;
[[nodiscard]] std::string SelectorCanonicalNodeId(const NodeSelector& selector);
[[nodiscard]] std::string_view ConfigErrorMessage(ConfigErrorCode code) noexcept;
[[nodiscard]] ConfigErrorCode LoadClusterConfigFile(
    const std::filesystem::path& path,
    ClusterConfig* output,
    std::string* error_message = nullptr);
[[nodiscard]] ConfigErrorCode SelectNodeConfig(
    const ClusterConfig& cluster_config,
    std::string_view selector,
    NodeConfig* output,
    std::string* error_message = nullptr);
[[nodiscard]] ConfigErrorCode LoadNodeConfigFile(
    const std::filesystem::path& path,
    std::string_view selector,
    NodeConfig* output,
    std::string* error_message = nullptr);

} // namespace xs::core

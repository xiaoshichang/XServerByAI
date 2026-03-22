#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace xs::node
{

struct NodeCommandLineArgs
{
    std::filesystem::path config_path{};
    std::string node_id{};
};

enum class NodeErrorCode : std::uint8_t
{
    None = 0,
    InvalidArgument,
    InvalidArgumentCount,
    EmptyConfigPath,
    EmptyNodeId,
    InvalidNodeId,
    ConfigLoadFailed,
    LoggerInitFailed,
    NodeCreateFailed,
    NodeInitFailed,
    NodeRunFailed,
    NodeUninitFailed,
    EventLoopFailed,
    UnsupportedProcessType,
};

[[nodiscard]] std::string_view NodeUsage() noexcept;
[[nodiscard]] std::string_view NodeErrorMessage(NodeErrorCode code) noexcept;

} // namespace xs::node

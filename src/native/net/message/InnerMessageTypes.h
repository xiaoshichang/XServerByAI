#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace xs::net
{

enum class InnerProcessType : std::uint16_t
{
    Gate = 1,
    Game = 2,
};

[[nodiscard]] inline bool IsValidInnerProcessType(std::uint16_t process_type) noexcept
{
    return process_type == static_cast<std::uint16_t>(InnerProcessType::Gate) ||
           process_type == static_cast<std::uint16_t>(InnerProcessType::Game);
}

inline constexpr std::size_t kInnerLoadSnapshotSize = sizeof(std::uint32_t) * 5u;

struct Endpoint
{
    std::string host{};
    std::uint16_t port{0};
};

struct LoadSnapshot
{
    std::uint32_t connection_count{0};
    std::uint32_t session_count{0};
    std::uint32_t entity_count{0};
    std::uint32_t space_count{0};
    std::uint32_t load_score{0};
};

} // namespace xs::net
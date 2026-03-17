#pragma once

#include <cstddef>
#include <cstdint>

namespace xs::net
{

enum class PacketFlag : std::uint16_t
{
    None = 0,
    Response = 1u << 0,
    Compressed = 1u << 1,
    Error = 1u << 2,
};

struct PacketHeader
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t flags;
    std::uint32_t length;
    std::uint32_t msg_id;
    std::uint32_t seq;
};

inline constexpr std::uint32_t kPacketMagic = 0x47535052u;
inline constexpr std::uint16_t kPacketVersion = 1u;
inline constexpr std::size_t kPacketHeaderSize = 20u;
inline constexpr std::uint16_t kPacketDefinedFlagMask =
    static_cast<std::uint16_t>(PacketFlag::Response) |
    static_cast<std::uint16_t>(PacketFlag::Compressed) |
    static_cast<std::uint16_t>(PacketFlag::Error);
inline constexpr std::uint32_t kPacketSeqNone = 0u;

static_assert(sizeof(PacketHeader) == kPacketHeaderSize, "PacketHeader wire size must remain 20 bytes");

} // namespace xs::net

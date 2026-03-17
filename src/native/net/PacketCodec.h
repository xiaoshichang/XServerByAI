#pragma once

#include "PacketHeader.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace xs::net
{

enum class PacketCodecErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidMagic = 3,
    UnsupportedVersion = 4,
    InvalidFlags = 5,
    LengthMismatch = 6,
    InvalidArgument = 7,
};

[[nodiscard]] std::string_view PacketCodecErrorMessage(PacketCodecErrorCode error_code) noexcept;

struct PacketView
{
    PacketHeader header{};
    std::span<const std::byte> payload{};
};

[[nodiscard]] bool IsValidPacketFlags(std::uint16_t flags) noexcept;
[[nodiscard]] PacketHeader MakePacketHeader(
    std::uint32_t msg_id,
    std::uint32_t seq,
    std::uint16_t flags,
    std::uint32_t payload_length) noexcept;
[[nodiscard]] PacketCodecErrorCode ValidatePacketHeader(const PacketHeader& header) noexcept;
[[nodiscard]] PacketCodecErrorCode GetPacketWireSize(std::size_t payload_size, std::size_t* wire_size) noexcept;
[[nodiscard]] PacketCodecErrorCode WritePacketHeader(const PacketHeader& header, std::span<std::byte> buffer) noexcept;
[[nodiscard]] PacketCodecErrorCode ReadPacketHeader(std::span<const std::byte> buffer, PacketHeader* header) noexcept;
[[nodiscard]] PacketCodecErrorCode EncodePacket(
    const PacketHeader& header,
    std::span<const std::byte> payload,
    std::span<std::byte> buffer) noexcept;
[[nodiscard]] PacketCodecErrorCode DecodePacket(std::span<const std::byte> buffer, PacketView* packet) noexcept;

} // namespace xs::net
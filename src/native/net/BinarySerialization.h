#pragma once

#include "ByteOrder.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace xs::net
{

enum class SerializationErrorCode : std::uint8_t
{
    None = 0,
    BufferTooSmall = 1,
    LengthOverflow = 2,
    InvalidBoolValue = 3,
    InvalidArgument = 4,
};

[[nodiscard]] std::string_view SerializationErrorMessage(SerializationErrorCode error_code) noexcept;

class BinaryWriter final
{
  public:
    explicit BinaryWriter(std::span<std::byte> buffer) noexcept;

    [[nodiscard]] bool WriteUInt8(std::uint8_t value) noexcept;
    [[nodiscard]] bool WriteUInt16(std::uint16_t value) noexcept;
    [[nodiscard]] bool WriteUInt32(std::uint32_t value) noexcept;
    [[nodiscard]] bool WriteUInt64(std::uint64_t value) noexcept;
    [[nodiscard]] bool WriteInt32(std::int32_t value) noexcept;
    [[nodiscard]] bool WriteBool(bool value) noexcept;
    [[nodiscard]] bool WriteBytes(std::span<const std::byte> value) noexcept;
    [[nodiscard]] bool WriteLengthPrefixedBytes16(std::span<const std::byte> value) noexcept;
    [[nodiscard]] bool WriteLengthPrefixedBytes32(std::span<const std::byte> value) noexcept;
    [[nodiscard]] bool WriteString16(std::string_view value) noexcept;

    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] SerializationErrorCode error() const noexcept;
    [[nodiscard]] std::span<const std::byte> written() const noexcept;

  private:
    [[nodiscard]] bool SetError(SerializationErrorCode error_code) noexcept;
    [[nodiscard]] bool SetSuccess() noexcept;
    [[nodiscard]] bool CanWrite(std::size_t size) noexcept;

    std::span<std::byte> buffer_{};
    std::size_t offset_{0};
    SerializationErrorCode error_{SerializationErrorCode::None};
};

class BinaryReader final
{
  public:
    explicit BinaryReader(std::span<const std::byte> buffer) noexcept;

    [[nodiscard]] bool ReadUInt8(std::uint8_t* value) noexcept;
    [[nodiscard]] bool ReadUInt16(std::uint16_t* value) noexcept;
    [[nodiscard]] bool ReadUInt32(std::uint32_t* value) noexcept;
    [[nodiscard]] bool ReadUInt64(std::uint64_t* value) noexcept;
    [[nodiscard]] bool ReadInt32(std::int32_t* value) noexcept;
    [[nodiscard]] bool ReadBool(bool* value) noexcept;
    [[nodiscard]] bool ReadBytes(std::size_t size, std::span<const std::byte>* value) noexcept;
    [[nodiscard]] bool ReadLengthPrefixedBytes16(std::span<const std::byte>* value) noexcept;
    [[nodiscard]] bool ReadLengthPrefixedBytes32(std::span<const std::byte>* value) noexcept;
    [[nodiscard]] bool ReadString16(std::string* value) noexcept;

    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] SerializationErrorCode error() const noexcept;

  private:
    [[nodiscard]] bool SetError(SerializationErrorCode error_code) noexcept;
    [[nodiscard]] bool SetSuccess() noexcept;
    [[nodiscard]] bool CanRead(std::size_t size) noexcept;

    std::span<const std::byte> buffer_{};
    std::size_t offset_{0};
    SerializationErrorCode error_{SerializationErrorCode::None};
};

} // namespace xs::net
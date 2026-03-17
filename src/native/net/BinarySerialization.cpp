#include "BinarySerialization.h"

#include <cstring>
#include <limits>

namespace xs::net {
namespace {

template <typename T>
bool WriteIntegral(std::span<std::byte> buffer, std::size_t* offset, T value) noexcept {
    const auto network_value = HostToNetwork(value);
    std::memcpy(buffer.data() + *offset, &network_value, sizeof(network_value));
    *offset += sizeof(network_value);
    return true;
}

template <typename T>
bool PeekIntegral(std::span<const std::byte> buffer, std::size_t offset, T* value) noexcept {
    T network_value{};
    std::memcpy(&network_value, buffer.data() + offset, sizeof(network_value));
    *value = NetworkToHost(network_value);
    return true;
}

template <typename T>
std::span<const std::byte> AsConstByteSpan(const T* data, std::size_t size) noexcept {
    return std::as_bytes(std::span(data, size));
}

} // namespace

std::string_view SerializationErrorMessage(SerializationErrorCode error_code) noexcept {
    switch (error_code) {
    case SerializationErrorCode::None:
        return "Success.";
    case SerializationErrorCode::BufferTooSmall:
        return "Buffer does not contain enough bytes for the requested operation.";
    case SerializationErrorCode::LengthOverflow:
        return "Value length exceeds the supported prefix range.";
    case SerializationErrorCode::InvalidBoolValue:
        return "Encoded bool must be 0 or 1.";
    case SerializationErrorCode::InvalidArgument:
        return "Output argument must not be null.";
    }

    return "Unknown serialization error.";
}

BinaryWriter::BinaryWriter(std::span<std::byte> buffer) noexcept
    : buffer_(buffer) {
}

bool BinaryWriter::WriteUInt8(std::uint8_t value) noexcept {
    if (!CanWrite(sizeof(value))) {
        return false;
    }

    return WriteIntegral(buffer_, &offset_, value) && SetSuccess();
}

bool BinaryWriter::WriteUInt16(std::uint16_t value) noexcept {
    if (!CanWrite(sizeof(value))) {
        return false;
    }

    return WriteIntegral(buffer_, &offset_, value) && SetSuccess();
}

bool BinaryWriter::WriteUInt32(std::uint32_t value) noexcept {
    if (!CanWrite(sizeof(value))) {
        return false;
    }

    return WriteIntegral(buffer_, &offset_, value) && SetSuccess();
}

bool BinaryWriter::WriteUInt64(std::uint64_t value) noexcept {
    if (!CanWrite(sizeof(value))) {
        return false;
    }

    return WriteIntegral(buffer_, &offset_, value) && SetSuccess();
}

bool BinaryWriter::WriteInt32(std::int32_t value) noexcept {
    if (!CanWrite(sizeof(value))) {
        return false;
    }

    return WriteIntegral(buffer_, &offset_, value) && SetSuccess();
}

bool BinaryWriter::WriteBool(bool value) noexcept {
    return WriteUInt8(value ? std::uint8_t{1} : std::uint8_t{0});
}

bool BinaryWriter::WriteBytes(std::span<const std::byte> value) noexcept {
    if (!CanWrite(value.size())) {
        return false;
    }

    if (!value.empty()) {
        std::memcpy(buffer_.data() + offset_, value.data(), value.size());
        offset_ += value.size();
    }

    return SetSuccess();
}

bool BinaryWriter::WriteLengthPrefixedBytes16(std::span<const std::byte> value) noexcept {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
        return SetError(SerializationErrorCode::LengthOverflow);
    }

    constexpr std::size_t kPrefixSize = sizeof(std::uint16_t);
    if (!CanWrite(kPrefixSize)) {
        return false;
    }

    if (value.size() > remaining() - kPrefixSize) {
        return SetError(SerializationErrorCode::BufferTooSmall);
    }

    const auto length = static_cast<std::uint16_t>(value.size());
    (void)WriteIntegral(buffer_, &offset_, length);
    if (!value.empty()) {
        std::memcpy(buffer_.data() + offset_, value.data(), value.size());
        offset_ += value.size();
    }

    return SetSuccess();
}

bool BinaryWriter::WriteLengthPrefixedBytes32(std::span<const std::byte> value) noexcept {
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return SetError(SerializationErrorCode::LengthOverflow);
    }

    constexpr std::size_t kPrefixSize = sizeof(std::uint32_t);
    if (!CanWrite(kPrefixSize)) {
        return false;
    }

    if (value.size() > remaining() - kPrefixSize) {
        return SetError(SerializationErrorCode::BufferTooSmall);
    }

    const auto length = static_cast<std::uint32_t>(value.size());
    (void)WriteIntegral(buffer_, &offset_, length);
    if (!value.empty()) {
        std::memcpy(buffer_.data() + offset_, value.data(), value.size());
        offset_ += value.size();
    }

    return SetSuccess();
}

bool BinaryWriter::WriteString16(std::string_view value) noexcept {
    return WriteLengthPrefixedBytes16(AsConstByteSpan(value.data(), value.size()));
}

std::size_t BinaryWriter::offset() const noexcept {
    return offset_;
}

std::size_t BinaryWriter::remaining() const noexcept {
    return buffer_.size() - offset_;
}

SerializationErrorCode BinaryWriter::error() const noexcept {
    return error_;
}

std::span<const std::byte> BinaryWriter::written() const noexcept {
    return std::span<const std::byte>(buffer_.data(), offset_);
}

bool BinaryWriter::SetError(SerializationErrorCode error_code) noexcept {
    error_ = error_code;
    return false;
}

bool BinaryWriter::SetSuccess() noexcept {
    error_ = SerializationErrorCode::None;
    return true;
}

bool BinaryWriter::CanWrite(std::size_t size) noexcept {
    if (size > remaining()) {
        return SetError(SerializationErrorCode::BufferTooSmall);
    }

    return true;
}

BinaryReader::BinaryReader(std::span<const std::byte> buffer) noexcept
    : buffer_(buffer) {
}

bool BinaryReader::ReadUInt8(std::uint8_t* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    if (!CanRead(sizeof(*value))) {
        return false;
    }

    *value = static_cast<std::uint8_t>(buffer_[offset_]);
    ++offset_;
    return SetSuccess();
}

bool BinaryReader::ReadUInt16(std::uint16_t* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    if (!CanRead(sizeof(*value))) {
        return false;
    }

    (void)PeekIntegral(buffer_, offset_, value);
    offset_ += sizeof(*value);
    return SetSuccess();
}

bool BinaryReader::ReadUInt32(std::uint32_t* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    if (!CanRead(sizeof(*value))) {
        return false;
    }

    (void)PeekIntegral(buffer_, offset_, value);
    offset_ += sizeof(*value);
    return SetSuccess();
}

bool BinaryReader::ReadUInt64(std::uint64_t* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    if (!CanRead(sizeof(*value))) {
        return false;
    }

    (void)PeekIntegral(buffer_, offset_, value);
    offset_ += sizeof(*value);
    return SetSuccess();
}

bool BinaryReader::ReadInt32(std::int32_t* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    if (!CanRead(sizeof(*value))) {
        return false;
    }

    (void)PeekIntegral(buffer_, offset_, value);
    offset_ += sizeof(*value);
    return SetSuccess();
}

bool BinaryReader::ReadBool(bool* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    if (!CanRead(sizeof(std::uint8_t))) {
        return false;
    }

    const auto raw_value = static_cast<std::uint8_t>(buffer_[offset_]);
    if (raw_value > 1u) {
        return SetError(SerializationErrorCode::InvalidBoolValue);
    }

    *value = raw_value == 1u;
    ++offset_;
    return SetSuccess();
}

bool BinaryReader::ReadBytes(std::size_t size, std::span<const std::byte>* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    if (!CanRead(size)) {
        return false;
    }

    *value = buffer_.subspan(offset_, size);
    offset_ += size;
    return SetSuccess();
}

bool BinaryReader::ReadLengthPrefixedBytes16(std::span<const std::byte>* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    std::uint16_t length = 0;
    constexpr std::size_t kPrefixSize = sizeof(length);
    if (!CanRead(kPrefixSize)) {
        return false;
    }

    (void)PeekIntegral(buffer_, offset_, &length);
    if (static_cast<std::size_t>(length) > remaining() - kPrefixSize) {
        return SetError(SerializationErrorCode::BufferTooSmall);
    }

    offset_ += kPrefixSize;
    *value = buffer_.subspan(offset_, length);
    offset_ += length;
    return SetSuccess();
}

bool BinaryReader::ReadLengthPrefixedBytes32(std::span<const std::byte>* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    std::uint32_t length = 0;
    constexpr std::size_t kPrefixSize = sizeof(length);
    if (!CanRead(kPrefixSize)) {
        return false;
    }

    (void)PeekIntegral(buffer_, offset_, &length);
    if (static_cast<std::size_t>(length) > remaining() - kPrefixSize) {
        return SetError(SerializationErrorCode::BufferTooSmall);
    }

    offset_ += kPrefixSize;
    *value = buffer_.subspan(offset_, static_cast<std::size_t>(length));
    offset_ += static_cast<std::size_t>(length);
    return SetSuccess();
}

bool BinaryReader::ReadString16(std::string* value) noexcept {
    if (value == nullptr) {
        return SetError(SerializationErrorCode::InvalidArgument);
    }

    std::span<const std::byte> bytes;
    if (!ReadLengthPrefixedBytes16(&bytes)) {
        return false;
    }

    value->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return SetSuccess();
}

std::size_t BinaryReader::offset() const noexcept {
    return offset_;
}

std::size_t BinaryReader::remaining() const noexcept {
    return buffer_.size() - offset_;
}

SerializationErrorCode BinaryReader::error() const noexcept {
    return error_;
}

bool BinaryReader::SetError(SerializationErrorCode error_code) noexcept {
    error_ = error_code;
    return false;
}

bool BinaryReader::SetSuccess() noexcept {
    error_ = SerializationErrorCode::None;
    return true;
}

bool BinaryReader::CanRead(std::size_t size) noexcept {
    if (size > remaining()) {
        return SetError(SerializationErrorCode::BufferTooSmall);
    }

    return true;
}

} // namespace xs::net
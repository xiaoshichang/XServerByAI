#pragma once

#include <bit>
#include <cstdint>
#include <type_traits>

namespace xs::net {
namespace detail {

template <typename T, bool = std::is_enum_v<std::remove_cv_t<T>>>
struct ByteOrderUnderlyingType {
    using type = std::remove_cv_t<T>;
};

template <typename T>
struct ByteOrderUnderlyingType<T, true> {
    using type = std::underlying_type_t<std::remove_cv_t<T>>;
};

template <typename T>
using ByteOrderUnderlying = typename ByteOrderUnderlyingType<T>::type;

template <typename T>
inline constexpr bool kByteOrderSupported =
    (std::is_integral_v<std::remove_cv_t<T>> || std::is_enum_v<std::remove_cv_t<T>>) &&
    !std::is_same_v<std::remove_cv_t<ByteOrderUnderlying<T>>, bool>;

constexpr std::uint16_t ByteSwap16(std::uint16_t value) noexcept {
    return static_cast<std::uint16_t>((value << 8u) | (value >> 8u));
}

constexpr std::uint32_t ByteSwap32(std::uint32_t value) noexcept {
    return ((value & 0x000000FFu) << 24u) |
           ((value & 0x0000FF00u) << 8u) |
           ((value & 0x00FF0000u) >> 8u) |
           ((value & 0xFF000000u) >> 24u);
}

constexpr std::uint64_t ByteSwap64(std::uint64_t value) noexcept {
    return ((value & 0x00000000000000FFull) << 56u) |
           ((value & 0x000000000000FF00ull) << 40u) |
           ((value & 0x0000000000FF0000ull) << 24u) |
           ((value & 0x00000000FF000000ull) << 8u) |
           ((value & 0x000000FF00000000ull) >> 8u) |
           ((value & 0x0000FF0000000000ull) >> 24u) |
           ((value & 0x00FF000000000000ull) >> 40u) |
           ((value & 0xFF00000000000000ull) >> 56u);
}

template <typename T>
constexpr T ByteSwap(T value) noexcept {
    static_assert(kByteOrderSupported<T>, "ByteSwap only supports fixed-width integral or enum types except bool.");

    using Underlying = ByteOrderUnderlying<T>;
    using Unsigned = std::make_unsigned_t<Underlying>;

    const auto unsigned_value = static_cast<Unsigned>(static_cast<Underlying>(value));

    if constexpr (sizeof(Underlying) == 1) {
        return value;
    } else if constexpr (sizeof(Underlying) == 2) {
        return static_cast<T>(static_cast<Underlying>(ByteSwap16(static_cast<std::uint16_t>(unsigned_value))));
    } else if constexpr (sizeof(Underlying) == 4) {
        return static_cast<T>(static_cast<Underlying>(ByteSwap32(static_cast<std::uint32_t>(unsigned_value))));
    } else if constexpr (sizeof(Underlying) == 8) {
        return static_cast<T>(static_cast<Underlying>(ByteSwap64(static_cast<std::uint64_t>(unsigned_value))));
    } else {
        static_assert(
            sizeof(Underlying) == 1 || sizeof(Underlying) == 2 || sizeof(Underlying) == 4 || sizeof(Underlying) == 8,
            "ByteSwap only supports 1/2/4/8-byte values.");
        return value;
    }
}

} // namespace detail

template <typename T>
[[nodiscard]] constexpr T HostToNetwork(T value) noexcept {
    static_assert(detail::kByteOrderSupported<T>, "HostToNetwork only supports fixed-width integral or enum types except bool.");
    static_assert(
        std::endian::native == std::endian::little || std::endian::native == std::endian::big,
        "Mixed-endian platforms are not supported.");

    if constexpr (std::endian::native == std::endian::little) {
        return detail::ByteSwap(value);
    } else {
        return value;
    }
}

template <typename T>
[[nodiscard]] constexpr T NetworkToHost(T value) noexcept {
    return HostToNetwork(value);
}

} // namespace xs::net
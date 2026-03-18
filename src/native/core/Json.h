#pragma once

#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace xs::core
{

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

enum class JsonErrorCode : std::uint8_t
{
    None = 0,
    InvalidArgument,
    ParseFailed,
    OpenReadFailed,
    InvalidIndent,
    PrepareDirectoryFailed,
    OpenWriteFailed,
    SerializeFailed,
    WriteFailed,
    DeserializeFailed,
};

[[nodiscard]] std::string_view JsonErrorMessage(JsonErrorCode code) noexcept;
[[nodiscard]] JsonErrorCode TryParseJson(std::string_view content, Json* output, std::string* error_message = nullptr);
[[nodiscard]] JsonErrorCode TryLoadJsonFile(
    const std::filesystem::path& path,
    Json* output,
    std::string* error_message = nullptr);
[[nodiscard]] JsonErrorCode SaveJsonFile(
    const std::filesystem::path& path,
    const Json& value,
    std::string* error_message = nullptr,
    int indent = 2,
    char indent_char = ' ');

template <typename T>
[[nodiscard]] Json SerializeJson(const T& value)
{
    return Json(value);
}

namespace detail
{

inline void ClearJsonErrorMessage(std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

inline JsonErrorCode SetJsonError(JsonErrorCode code, std::string message, std::string* error_message)
{
    if (error_message != nullptr)
    {
        *error_message = std::move(message);
    }
    return code;
}

} // namespace detail

template <typename T>
[[nodiscard]] JsonErrorCode TryDeserializeJson(const Json& source, T* output, std::string* error_message = nullptr)
{
    if (output == nullptr)
    {
        return detail::SetJsonError(
            JsonErrorCode::InvalidArgument,
            "JSON destination object must not be null.",
            error_message);
    }

    try
    {
        source.get_to(*output);
        detail::ClearJsonErrorMessage(error_message);
        return JsonErrorCode::None;
    }
    catch (const std::exception& exception)
    {
        return detail::SetJsonError(
            JsonErrorCode::DeserializeFailed,
            std::string{"Failed to deserialize JSON: "} + exception.what(),
            error_message);
    }
}

template <typename T>
[[nodiscard]] JsonErrorCode TryParseJsonAs(std::string_view content, T* output, std::string* error_message = nullptr)
{
    Json json_value;
    const JsonErrorCode parse_result = TryParseJson(content, &json_value, error_message);
    if (parse_result != JsonErrorCode::None)
    {
        return parse_result;
    }

    return TryDeserializeJson(json_value, output, error_message);
}

template <typename T>
[[nodiscard]] JsonErrorCode TryLoadJsonFileAs(
    const std::filesystem::path& path,
    T* output,
    std::string* error_message = nullptr)
{
    Json json_value;
    const JsonErrorCode load_result = TryLoadJsonFile(path, &json_value, error_message);
    if (load_result != JsonErrorCode::None)
    {
        return load_result;
    }

    return TryDeserializeJson(json_value, output, error_message);
}

template <typename T>
[[nodiscard]] JsonErrorCode SaveJsonFileFrom(
    const std::filesystem::path& path,
    const T& value,
    std::string* error_message = nullptr,
    int indent = 2,
    char indent_char = ' ')
{
    return SaveJsonFile(path, SerializeJson(value), error_message, indent, indent_char);
}

} // namespace xs::core

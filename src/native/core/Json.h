#pragma once

#include <exception>
#include <filesystem>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace xs::core
{

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

[[nodiscard]] bool TryParseJson(std::string_view content, Json* output, std::string* error_message = nullptr);
[[nodiscard]] bool TryLoadJsonFile(const std::filesystem::path& path, Json* output, std::string* error_message = nullptr);
[[nodiscard]] bool SaveJsonFile(
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

template <typename T>
[[nodiscard]] bool TryDeserializeJson(const Json& source, T* output, std::string* error_message = nullptr)
{
    if (output == nullptr)
    {
        if (error_message != nullptr)
        {
            *error_message = "JSON destination object must not be null.";
        }
        return false;
    }

    try
    {
        source.get_to(*output);
        if (error_message != nullptr)
        {
            error_message->clear();
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        if (error_message != nullptr)
        {
            *error_message = std::string{"Failed to deserialize JSON: "} + exception.what();
        }
        return false;
    }
}

template <typename T>
[[nodiscard]] bool TryParseJsonAs(std::string_view content, T* output, std::string* error_message = nullptr)
{
    Json json_value;
    if (!TryParseJson(content, &json_value, error_message))
    {
        return false;
    }

    return TryDeserializeJson(json_value, output, error_message);
}

template <typename T>
[[nodiscard]] bool TryLoadJsonFileAs(
    const std::filesystem::path& path,
    T* output,
    std::string* error_message = nullptr)
{
    Json json_value;
    if (!TryLoadJsonFile(path, &json_value, error_message))
    {
        return false;
    }

    return TryDeserializeJson(json_value, output, error_message);
}

template <typename T>
[[nodiscard]] bool SaveJsonFileFrom(
    const std::filesystem::path& path,
    const T& value,
    std::string* error_message = nullptr,
    int indent = 2,
    char indent_char = ' ')
{
    return SaveJsonFile(path, SerializeJson(value), error_message, indent, indent_char);
}

} // namespace xs::core

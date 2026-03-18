#include "Json.h"

#include <fstream>
#include <utility>

namespace
{

void ClearErrorMessage(std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

xs::core::JsonErrorCode SetErrorMessage(
    xs::core::JsonErrorCode code,
    std::string message,
    std::string* error_message)
{
    if (error_message != nullptr)
    {
        *error_message = std::move(message);
    }
    return code;
}

std::string PathToUtf8(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const auto raw = path.u8string();
    return std::string(raw.begin(), raw.end());
#else
    return path.string();
#endif
}

xs::core::JsonErrorCode FailWithPath(
    xs::core::JsonErrorCode code,
    std::string_view prefix,
    const std::filesystem::path& path,
    std::string* error_message)
{
    std::string message{prefix};
    message += PathToUtf8(path);
    return SetErrorMessage(code, std::move(message), error_message);
}

xs::core::JsonErrorCode FailWithException(
    xs::core::JsonErrorCode code,
    std::string_view prefix,
    const std::exception& exception,
    std::string* error_message)
{
    std::string message{prefix};
    message += exception.what();
    return SetErrorMessage(code, std::move(message), error_message);
}

xs::core::JsonErrorCode FailWithPathException(
    xs::core::JsonErrorCode code,
    std::string_view prefix,
    const std::filesystem::path& path,
    const std::exception& exception,
    std::string* error_message)
{
    std::string message{prefix};
    message += PathToUtf8(path);
    message += ": ";
    message += exception.what();
    return SetErrorMessage(code, std::move(message), error_message);
}

} // namespace

namespace xs::core
{

std::string_view JsonErrorMessage(JsonErrorCode code) noexcept
{
    switch (code)
    {
    case JsonErrorCode::None:
        return "No error.";
    case JsonErrorCode::InvalidArgument:
        return "Invalid JSON API argument.";
    case JsonErrorCode::ParseFailed:
        return "Failed to parse JSON.";
    case JsonErrorCode::OpenReadFailed:
        return "Failed to open JSON file for reading.";
    case JsonErrorCode::InvalidIndent:
        return "JSON indentation must be -1 or greater.";
    case JsonErrorCode::PrepareDirectoryFailed:
        return "Failed to prepare JSON output directory.";
    case JsonErrorCode::OpenWriteFailed:
        return "Failed to open JSON file for writing.";
    case JsonErrorCode::SerializeFailed:
        return "Failed to serialize JSON.";
    case JsonErrorCode::WriteFailed:
        return "Failed to write JSON file.";
    case JsonErrorCode::DeserializeFailed:
        return "Failed to deserialize JSON.";
    }

    return "Unknown JSON error.";
}

JsonErrorCode TryParseJson(std::string_view content, Json* output, std::string* error_message)
{
    if (output == nullptr)
    {
        return SetErrorMessage(JsonErrorCode::InvalidArgument, "Parsed JSON output must not be null.", error_message);
    }

    try
    {
        *output = Json::parse(content.begin(), content.end());
        ClearErrorMessage(error_message);
        return JsonErrorCode::None;
    }
    catch (const std::exception& exception)
    {
        return FailWithException(JsonErrorCode::ParseFailed, "Failed to parse JSON: ", exception, error_message);
    }
}

JsonErrorCode TryLoadJsonFile(const std::filesystem::path& path, Json* output, std::string* error_message)
{
    if (output == nullptr)
    {
        return SetErrorMessage(JsonErrorCode::InvalidArgument, "Parsed JSON output must not be null.", error_message);
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return FailWithPath(JsonErrorCode::OpenReadFailed, "Failed to open JSON file for reading: ", path, error_message);
    }

    try
    {
        *output = Json::parse(input);
        ClearErrorMessage(error_message);
        return JsonErrorCode::None;
    }
    catch (const std::exception& exception)
    {
        return FailWithPathException(
            JsonErrorCode::ParseFailed,
            "Failed to parse JSON file: ",
            path,
            exception,
            error_message);
    }
}

JsonErrorCode SaveJsonFile(
    const std::filesystem::path& path,
    const Json& value,
    std::string* error_message,
    int indent,
    char indent_char)
{
    if (indent < -1)
    {
        return SetErrorMessage(JsonErrorCode::InvalidIndent, "JSON indentation must be -1 or greater.", error_message);
    }

    try
    {
        const auto parent_path = path.parent_path();
        if (!parent_path.empty())
        {
            std::filesystem::create_directories(parent_path);
        }
    }
    catch (const std::exception& exception)
    {
        return FailWithPathException(
            JsonErrorCode::PrepareDirectoryFailed,
            "Failed to prepare JSON output directory for: ",
            path,
            exception,
            error_message);
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        return FailWithPath(JsonErrorCode::OpenWriteFailed, "Failed to open JSON file for writing: ", path, error_message);
    }

    try
    {
        output << value.dump(indent, indent_char, false) << '\n';
    }
    catch (const std::exception& exception)
    {
        return FailWithPathException(
            JsonErrorCode::SerializeFailed,
            "Failed to serialize JSON file: ",
            path,
            exception,
            error_message);
    }

    if (!output.good())
    {
        return FailWithPath(JsonErrorCode::WriteFailed, "Failed to write JSON file: ", path, error_message);
    }

    ClearErrorMessage(error_message);
    return JsonErrorCode::None;
}

} // namespace xs::core

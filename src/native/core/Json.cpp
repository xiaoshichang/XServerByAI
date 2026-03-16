#include "Json.h"

#include <fstream>
#include <utility>

namespace {

void ClearErrorMessage(std::string* error_message) {
    if (error_message != nullptr) {
        error_message->clear();
    }
}

bool SetErrorMessage(std::string message, std::string* error_message) {
    if (error_message != nullptr) {
        *error_message = std::move(message);
    }
    return false;
}

std::string PathToUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto raw = path.u8string();
    return std::string(raw.begin(), raw.end());
#else
    return path.string();
#endif
}

bool FailWithPath(std::string_view prefix, const std::filesystem::path& path, std::string* error_message) {
    std::string message{prefix};
    message += PathToUtf8(path);
    return SetErrorMessage(std::move(message), error_message);
}

bool FailWithException(std::string_view prefix, const std::exception& exception, std::string* error_message) {
    std::string message{prefix};
    message += exception.what();
    return SetErrorMessage(std::move(message), error_message);
}

bool FailWithPathException(
    std::string_view prefix,
    const std::filesystem::path& path,
    const std::exception& exception,
    std::string* error_message) {
    std::string message{prefix};
    message += PathToUtf8(path);
    message += ": ";
    message += exception.what();
    return SetErrorMessage(std::move(message), error_message);
}

} // namespace

namespace xs::core {

bool TryParseJson(std::string_view content, Json* output, std::string* error_message) {
    if (output == nullptr) {
        return SetErrorMessage("Parsed JSON output must not be null.", error_message);
    }

    try {
        *output = Json::parse(content.begin(), content.end());
        ClearErrorMessage(error_message);
        return true;
    } catch (const std::exception& exception) {
        return FailWithException("Failed to parse JSON: ", exception, error_message);
    }
}

bool TryLoadJsonFile(const std::filesystem::path& path, Json* output, std::string* error_message) {
    if (output == nullptr) {
        return SetErrorMessage("Parsed JSON output must not be null.", error_message);
    }

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return FailWithPath("Failed to open JSON file for reading: ", path, error_message);
    }

    try {
        *output = Json::parse(input);
        ClearErrorMessage(error_message);
        return true;
    } catch (const std::exception& exception) {
        return FailWithPathException("Failed to parse JSON file: ", path, exception, error_message);
    }
}

bool SaveJsonFile(
    const std::filesystem::path& path,
    const Json& value,
    std::string* error_message,
    int indent,
    char indent_char) {
    if (indent < -1) {
        return SetErrorMessage("JSON indentation must be -1 or greater.", error_message);
    }

    try {
        const auto parent_path = path.parent_path();
        if (!parent_path.empty()) {
            std::filesystem::create_directories(parent_path);
        }
    } catch (const std::exception& exception) {
        return FailWithPathException("Failed to prepare JSON output directory for: ", path, exception, error_message);
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return FailWithPath("Failed to open JSON file for writing: ", path, error_message);
    }

    try {
        output << value.dump(indent, indent_char, false) << '\n';
    } catch (const std::exception& exception) {
        return FailWithPathException("Failed to serialize JSON file: ", path, exception, error_message);
    }

    if (!output.good()) {
        return FailWithPath("Failed to write JSON file: ", path, error_message);
    }

    ClearErrorMessage(error_message);
    return true;
}

} // namespace xs::core

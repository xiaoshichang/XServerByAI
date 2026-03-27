#include "Config.h"
#include "Json.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace xs::core
{

std::string_view ConfigErrorMessage(ConfigErrorCode code) noexcept;

namespace
{

enum class ParsedNodeKind : std::uint8_t
{
    Gm,
    Gate,
    Game,
};

struct ParsedNodeId
{
    ParsedNodeKind kind{ParsedNodeKind::Gm};
    std::string value{"GM"};
};

void ClearError(std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

bool SetError(std::string message, std::string* error_message)
{
    if (error_message != nullptr)
    {
        *error_message = std::move(message);
    }
    return false;
}

ConfigErrorCode SetConfigError(ConfigErrorCode code, std::string message, std::string* error_message)
{
    if (error_message != nullptr)
    {
        if (message.empty())
        {
            *error_message = std::string(ConfigErrorMessage(code));
        }
        else
        {
            *error_message = std::move(message);
        }
    }
    return code;
}

bool MessageMentionsLoggingPath(std::string_view message) noexcept
{
    return message.find("logging.") != std::string_view::npos;
}

ConfigErrorCode ClassifyConfigError(std::string_view message) noexcept
{
    if (message.find("must be an object.") != std::string_view::npos)
    {
        return ConfigErrorCode::ExpectedObject;
    }

    if (message.starts_with("Unknown field '"))
    {
        return ConfigErrorCode::UnknownField;
    }

    if (message.starts_with("Missing required field '"))
    {
        return ConfigErrorCode::MissingRequiredField;
    }

    if (message.find(" has invalid nodeId '") != std::string_view::npos ||
        message == "nodeId must be one of GM, Gate<index>, or Game<index>.")
    {
        return ConfigErrorCode::InvalidNodeId;
    }

    if (message.starts_with("Missing gate instance for nodeId '") ||
        message.starts_with("Missing game instance for nodeId '"))
    {
        return ConfigErrorCode::MissingInstance;
    }

    if (message.find("must contain at least one instance.") != std::string_view::npos)
    {
        return ConfigErrorCode::EmptyCollection;
    }

    if (message.find("must be one of Trace, Debug, Info, Warn, Error, Fatal.") != std::string_view::npos)
    {
        return ConfigErrorCode::InvalidLogLevel;
    }

    if (message.find("must be in the range 1-65535.") != std::string_view::npos)
    {
        return ConfigErrorCode::InvalidEndpointPort;
    }

    if (MessageMentionsLoggingPath(message) &&
        (message.find("must not be empty.") != std::string_view::npos ||
         message.find("must be greater than zero.") != std::string_view::npos))
    {
        return ConfigErrorCode::LoggingConfigInvalid;
    }

    if (message.find("must be a string.") != std::string_view::npos)
    {
        return ConfigErrorCode::InvalidString;
    }

    if (message.find("must not be empty.") != std::string_view::npos)
    {
        return ConfigErrorCode::EmptyString;
    }

    if (message.find("must be a boolean.") != std::string_view::npos)
    {
        return ConfigErrorCode::InvalidBoolean;
    }

    if (message.find("must be an unsigned integer.") != std::string_view::npos)
    {
        return ConfigErrorCode::InvalidUnsignedInteger;
    }

    if (message.find("is out of range.") != std::string_view::npos ||
        message.find("must be greater than zero.") != std::string_view::npos)
    {
        return ConfigErrorCode::ValueOutOfRange;
    }

    return ConfigErrorCode::Unknown;
}

std::string JoinPath(std::string_view parent, std::string_view child)
{
    if (parent.empty())
    {
        return std::string(child);
    }

    std::string path{parent};
    path.push_back('.');
    path.append(child);
    return path;
}

std::filesystem::path ResolveConfigRelativePath(
    const std::filesystem::path& config_base_path,
    std::string_view raw_path)
{
    std::filesystem::path resolved_path{std::string(raw_path)};
    if (resolved_path.is_absolute())
    {
        return resolved_path.lexically_normal();
    }

    if (config_base_path.empty())
    {
        return resolved_path.lexically_normal();
    }

    return (config_base_path / resolved_path).lexically_normal();
}
bool HasDigitsSuffix(std::string_view value, std::size_t prefix_length) noexcept
{
    if (value.size() <= prefix_length)
    {
        return false;
    }

    for (std::size_t index = prefix_length; index < value.size(); ++index)
    {
        const char ch = value[index];
        if (ch < '0' || ch > '9')
        {
            return false;
        }
    }

    return true;
}

std::optional<ParsedNodeId> ParseNodeId(std::string_view node_id) noexcept
{
    if (node_id == "GM")
    {
        return ParsedNodeId{ParsedNodeKind::Gm, std::string(node_id)};
    }

    if (node_id.starts_with("Gate") && HasDigitsSuffix(node_id, 4U))
    {
        return ParsedNodeId{ParsedNodeKind::Gate, std::string(node_id)};
    }

    if (node_id.starts_with("Game") && HasDigitsSuffix(node_id, 4U))
    {
        return ParsedNodeId{ParsedNodeKind::Game, std::string(node_id)};
    }

    return std::nullopt;
}

bool ValidateNodeIdKind(
    std::string_view node_id,
    ParsedNodeKind expected_kind,
    std::string_view path,
    std::string* error_message)
{
    const auto parsed_node_id = ParseNodeId(node_id);
    if (parsed_node_id.has_value() && parsed_node_id->kind == expected_kind)
    {
        return true;
    }

    std::ostringstream stream;
    stream << path << " has invalid nodeId '" << node_id << "'.";
    return SetError(stream.str(), error_message);
}

template <std::size_t N>
bool RejectUnknownFields(
    const Json& object,
    const std::array<std::string_view, N>& allowed_fields,
    std::string_view path,
    std::string* error_message)
{
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator)
    {
        const std::string_view key = iterator.key();
        const auto match = std::find(allowed_fields.begin(), allowed_fields.end(), key);
        if (match == allowed_fields.end())
        {
            std::ostringstream stream;
            stream << "Unknown field '" << iterator.key() << "' at " << path << '.';
            return SetError(stream.str(), error_message);
        }
    }

    return true;
}

bool ExpectObject(const Json& value, std::string_view path, std::string* error_message)
{
    if (!value.is_object())
    {
        std::ostringstream stream;
        stream << path << " must be an object.";
        return SetError(stream.str(), error_message);
    }

    return true;
}

bool TryGetMember(const Json& object, std::string_view key, const Json** member)
{
    const auto iterator = object.find(std::string(key));
    if (iterator == object.end())
    {
        *member = nullptr;
        return false;
    }

    *member = &(*iterator);
    return true;
}

bool GetRequiredMember(
    const Json& object,
    std::string_view key,
    const Json** member,
    std::string_view path,
    std::string* error_message)
{
    if (!TryGetMember(object, key, member))
    {
        std::ostringstream stream;
        stream << "Missing required field '" << JoinPath(path, key) << "'.";
        return SetError(stream.str(), error_message);
    }

    return true;
}

bool ParseString(const Json& value, std::string_view path, std::string* output, std::string* error_message)
{
    if (!value.is_string())
    {
        std::ostringstream stream;
        stream << path << " must be a string.";
        return SetError(stream.str(), error_message);
    }

    *output = value.get<std::string>();
    return true;
}

bool ParseNonEmptyString(const Json& value, std::string_view path, std::string* output, std::string* error_message)
{
    if (!ParseString(value, path, output, error_message))
    {
        return false;
    }

    if (output->empty())
    {
        std::ostringstream stream;
        stream << path << " must not be empty.";
        return SetError(stream.str(), error_message);
    }

    return true;
}

bool ParseBool(const Json& value, std::string_view path, bool* output, std::string* error_message)
{
    if (!value.is_boolean())
    {
        std::ostringstream stream;
        stream << path << " must be a boolean.";
        return SetError(stream.str(), error_message);
    }

    *output = value.get<bool>();
    return true;
}

template <typename T>
bool ParseUnsignedInteger(const Json& value, std::string_view path, T* output, std::string* error_message)
{
    static_assert(std::numeric_limits<T>::is_integer, "T must be an integer type.");
    static_assert(!std::numeric_limits<T>::is_signed, "T must be unsigned.");

    std::uint64_t parsed_value = 0U;
    if (value.is_number_unsigned())
    {
        parsed_value = value.get<std::uint64_t>();
    }
    else if (value.is_number_integer())
    {
        const auto signed_value = value.get<std::int64_t>();
        if (signed_value < 0)
        {
            std::ostringstream stream;
            stream << path << " must be an unsigned integer.";
            return SetError(stream.str(), error_message);
        }

        parsed_value = static_cast<std::uint64_t>(signed_value);
    }
    else
    {
        std::ostringstream stream;
        stream << path << " must be an unsigned integer.";
        return SetError(stream.str(), error_message);
    }

    if (parsed_value > static_cast<std::uint64_t>(std::numeric_limits<T>::max()))
    {
        std::ostringstream stream;
        stream << path << " is out of range.";
        return SetError(stream.str(), error_message);
    }

    *output = static_cast<T>(parsed_value);
    return true;
}

bool ValidateLoggingConfigAtPath(const LoggingConfig& config, std::string_view path, std::string* error_message)
{
    const LoggingErrorCode validation_result = ValidateLoggingConfig(config, nullptr);
    if (validation_result == LoggingErrorCode::None)
    {
        ClearError(error_message);
        return true;
    }

    std::ostringstream stream;
    switch (validation_result)
    {
    case LoggingErrorCode::EmptyRootDir:
        stream << JoinPath(path, "rootDir") << " must not be empty.";
        break;
    case LoggingErrorCode::FlushIntervalMustBePositive:
        stream << JoinPath(path, "flushIntervalMs") << " must be greater than zero.";
        break;
    case LoggingErrorCode::MaxFileSizeMustBePositive:
        stream << JoinPath(path, "maxFileSizeMB") << " must be greater than zero.";
        break;
    case LoggingErrorCode::MaxRetainedFilesMustBePositive:
        stream << JoinPath(path, "maxRetainedFiles") << " must be greater than zero.";
        break;
    case LoggingErrorCode::None:
        break;
    }

    return SetError(stream.str(), error_message);
}

bool ParseLoggingBlock(
    const Json& value,
    const LoggingConfig& base_config,
    LoggingConfig* output,
    std::string_view path,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 6> kAllowedFields{
        "rootDir",
        "minLevel",
        "flushIntervalMs",
        "rotateDaily",
        "maxFileSizeMB",
        "maxRetainedFiles",
    };

    if (output == nullptr)
    {
        return SetError("Logging config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    LoggingConfig config = base_config;
    const Json* member = nullptr;

    if (TryGetMember(value, "rootDir", &member))
    {
        if (!ParseString(*member, JoinPath(path, "rootDir"), &config.root_dir, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "minLevel", &member))
    {
        std::string level_name;
        const std::string level_path = JoinPath(path, "minLevel");
        if (!ParseNonEmptyString(*member, level_path, &level_name, error_message))
        {
            return false;
        }

        const auto parsed_level = ParseLogLevel(level_name);
        if (!parsed_level.has_value())
        {
            std::ostringstream stream;
            stream << level_path << " must be one of Trace, Debug, Info, Warn, Error, Fatal.";
            return SetError(stream.str(), error_message);
        }

        config.min_level = *parsed_level;
    }

    if (TryGetMember(value, "flushIntervalMs", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "flushIntervalMs"), &config.flush_interval_ms, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "rotateDaily", &member))
    {
        if (!ParseBool(*member, JoinPath(path, "rotateDaily"), &config.rotate_daily, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "maxFileSizeMB", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "maxFileSizeMB"), &config.max_file_size_mb, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "maxRetainedFiles", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "maxRetainedFiles"), &config.max_retained_files, error_message))
        {
            return false;
        }
    }

    if (!ValidateLoggingConfigAtPath(config, path, error_message))
    {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseEndpointConfig(
    const Json& value,
    EndpointConfig* output,
    std::string_view path,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 2> kAllowedFields{
        "host",
        "port",
    };

    if (output == nullptr)
    {
        return SetError("Endpoint config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    const Json* host_value = nullptr;
    const Json* port_value = nullptr;
    if (!GetRequiredMember(value, "host", &host_value, path, error_message) ||
        !GetRequiredMember(value, "port", &port_value, path, error_message))
    {
        return false;
    }

    EndpointConfig endpoint;
    if (!ParseNonEmptyString(*host_value, JoinPath(path, "host"), &endpoint.host, error_message))
    {
        return false;
    }

    std::uint32_t port = 0U;
    if (!ParseUnsignedInteger(*port_value, JoinPath(path, "port"), &port, error_message))
    {
        return false;
    }

    if (port == 0U || port > std::numeric_limits<std::uint16_t>::max())
    {
        std::ostringstream stream;
        stream << JoinPath(path, "port") << " must be in the range 1-65535.";
        return SetError(stream.str(), error_message);
    }

    endpoint.port = static_cast<std::uint16_t>(port);
    *output = std::move(endpoint);
    ClearError(error_message);
    return true;
}

bool ParseListenEndpointContainer(
    const Json& value,
    EndpointConfig* output,
    std::string_view path,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 1> kAllowedFields{
        "listenEndpoint",
    };

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    const Json* endpoint_value = nullptr;
    if (!GetRequiredMember(value, "listenEndpoint", &endpoint_value, path, error_message))
    {
        return false;
    }

    return ParseEndpointConfig(*endpoint_value, output, JoinPath(path, "listenEndpoint"), error_message);
}

bool ParseEnvConfig(
    const Json& value,
    EnvConfig* output,
    std::string_view path,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 2> kAllowedFields{
        "id",
        "environment",
    };

    if (output == nullptr)
    {
        return SetError("Env config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    const Json* id_value = nullptr;
    const Json* environment_value = nullptr;
    if (!GetRequiredMember(value, "id", &id_value, path, error_message) ||
        !GetRequiredMember(value, "environment", &environment_value, path, error_message))
    {
        return false;
    }

    EnvConfig config;
    if (!ParseNonEmptyString(*id_value, JoinPath(path, "id"), &config.id, error_message) ||
        !ParseNonEmptyString(*environment_value, JoinPath(path, "environment"), &config.environment, error_message))
    {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ValidateKcpConfig(const KcpConfig& config, std::string_view path, std::string* error_message)
{
    const auto fail_positive = [path, error_message](std::string_view key) {
        std::ostringstream stream;
        stream << JoinPath(path, key) << " must be greater than zero.";
        return SetError(stream.str(), error_message);
    };

    if (config.mtu == 0U)
    {
        return fail_positive("mtu");
    }

    if (config.sndwnd == 0U)
    {
        return fail_positive("sndwnd");
    }

    if (config.rcvwnd == 0U)
    {
        return fail_positive("rcvwnd");
    }

    if (config.interval_ms == 0U)
    {
        return fail_positive("intervalMs");
    }

    if (config.min_rto_ms == 0U)
    {
        return fail_positive("minRtoMs");
    }

    if (config.dead_link_count == 0U)
    {
        return fail_positive("deadLinkCount");
    }

    ClearError(error_message);
    return true;
}

bool ParseKcpConfig(const Json& value, KcpConfig* output, std::string_view path, std::string* error_message)
{
    static constexpr std::array<std::string_view, 10> kAllowedFields{
        "mtu",
        "sndwnd",
        "rcvwnd",
        "nodelay",
        "intervalMs",
        "fastResend",
        "noCongestionWindow",
        "minRtoMs",
        "deadLinkCount",
        "streamMode",
    };

    if (output == nullptr)
    {
        return SetError("KCP config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    KcpConfig config = *output;
    const Json* member = nullptr;

    if (TryGetMember(value, "mtu", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "mtu"), &config.mtu, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "sndwnd", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "sndwnd"), &config.sndwnd, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "rcvwnd", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "rcvwnd"), &config.rcvwnd, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "nodelay", &member))
    {
        if (!ParseBool(*member, JoinPath(path, "nodelay"), &config.nodelay, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "intervalMs", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "intervalMs"), &config.interval_ms, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "fastResend", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "fastResend"), &config.fast_resend, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "noCongestionWindow", &member))
    {
        if (!ParseBool(*member, JoinPath(path, "noCongestionWindow"), &config.no_congestion_window, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "minRtoMs", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "minRtoMs"), &config.min_rto_ms, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "deadLinkCount", &member))
    {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "deadLinkCount"), &config.dead_link_count, error_message))
        {
            return false;
        }
    }

    if (TryGetMember(value, "streamMode", &member))
    {
        if (!ParseBool(*member, JoinPath(path, "streamMode"), &config.stream_mode, error_message))
        {
            return false;
        }
    }

    if (!ValidateKcpConfig(config, path, error_message))
    {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseManagedConfig(
    const Json& value,
    ManagedConfig* output,
    const std::filesystem::path& config_base_path,
    std::string_view path,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 3> kAllowedFields{
        "assemblyName",
        "assemblyPath",
        "runtimeConfigPath",
    };

    if (output == nullptr)
    {
        return SetError("Managed config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    ManagedConfig config = *output;
    const Json* assembly_name = nullptr;
    if (TryGetMember(value, "assemblyName", &assembly_name))
    {
        if (!ParseNonEmptyString(*assembly_name, JoinPath(path, "assemblyName"), &config.assembly_name, error_message))
        {
            return false;
        }
    }

    const auto parse_asset_path =
        [&](std::string_view key, std::filesystem::path* output_path) {
            const Json* member = nullptr;
            if (!TryGetMember(value, key, &member))
            {
                return true;
            }

            std::string raw_path;
            if (!ParseNonEmptyString(*member, JoinPath(path, key), &raw_path, error_message))
            {
                return false;
            }

            *output_path = ResolveConfigRelativePath(config_base_path, raw_path);
            return true;
        };

    if (!parse_asset_path("assemblyPath", &config.assembly_path) ||
        !parse_asset_path("runtimeConfigPath", &config.runtime_config_path))
    {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}
bool ParseGmConfig(
    const Json& value,
    GmConfig* output,
    std::string_view path,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 2> kAllowedFields{
        "innerNetwork",
        "controlNetwork",
    };

    if (output == nullptr)
    {
        return SetError("GM config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    GmConfig config;

    const Json* inner_network = nullptr;
    const Json* control_network = nullptr;
    if (!GetRequiredMember(value, "innerNetwork", &inner_network, path, error_message))
    {
        return false;
    }

    if (!GetRequiredMember(value, "controlNetwork", &control_network, path, error_message))
    {
        return false;
    }

    if (!ParseListenEndpointContainer(
            *inner_network,
            &config.inner_network_listen_endpoint,
            JoinPath(path, "innerNetwork"),
            error_message))
    {
        return false;
    }

    if (!ParseListenEndpointContainer(
            *control_network,
            &config.control_network_listen_endpoint,
            JoinPath(path, "controlNetwork"),
            error_message))
    {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseGateConfig(
    const Json& value,
    std::string_view node_id,
    GateConfig* output,
    std::string_view path,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 2> kAllowedFields{
        "innerNetwork",
        "clientNetwork",
    };

    if (output == nullptr)
    {
        return SetError("Gate config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    if (!ValidateNodeIdKind(node_id, ParsedNodeKind::Gate, path, error_message))
    {
        return false;
    }

    GateConfig config;
    const Json* inner_network = nullptr;
    const Json* client_network = nullptr;
    if (!GetRequiredMember(value, "innerNetwork", &inner_network, path, error_message))
    {
        return false;
    }

    if (!GetRequiredMember(value, "clientNetwork", &client_network, path, error_message))
    {
        return false;
    }

    if (!ParseListenEndpointContainer(
            *inner_network,
            &config.inner_network_listen_endpoint,
            JoinPath(path, "innerNetwork"),
            error_message))
    {
        return false;
    }

    if (!ParseListenEndpointContainer(
            *client_network,
            &config.client_network_listen_endpoint,
            JoinPath(path, "clientNetwork"),
            error_message))
    {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseGameConfig(
    const Json& value,
    std::string_view node_id,
    GameConfig* output,
    std::string_view path,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 1> kAllowedFields{
        "innerNetwork",
    };

    if (output == nullptr)
    {
        return SetError("Game config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message))
    {
        return false;
    }

    if (!ValidateNodeIdKind(node_id, ParsedNodeKind::Game, path, error_message))
    {
        return false;
    }

    GameConfig config;
    const Json* inner_network = nullptr;
    if (!GetRequiredMember(value, "innerNetwork", &inner_network, path, error_message))
    {
        return false;
    }

    if (!ParseListenEndpointContainer(
            *inner_network,
            &config.inner_network_listen_endpoint,
            JoinPath(path, "innerNetwork"),
            error_message))
    {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}
bool ParseGateCollection(
    const Json& value,
    std::map<std::string, GateConfig, std::less<>>* output,
    std::string_view path,
    std::string* error_message)
{
    if (output == nullptr)
    {
        return SetError("Gate collection output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (value.empty())
    {
        std::ostringstream stream;
        stream << path << " must contain at least one instance.";
        return SetError(stream.str(), error_message);
    }

    output->clear();
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
    {
        GateConfig config;
        const std::string instance_path = JoinPath(path, iterator.key());
        if (!ParseGateConfig(iterator.value(), iterator.key(), &config, instance_path, error_message))
        {
            return false;
        }

        output->emplace(iterator.key(), std::move(config));
    }

    ClearError(error_message);
    return true;
}

bool ParseGameCollection(
    const Json& value,
    std::map<std::string, GameConfig, std::less<>>* output,
    std::string_view path,
    std::string* error_message)
{
    if (output == nullptr)
    {
        return SetError("Game collection output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message))
    {
        return false;
    }

    if (value.empty())
    {
        std::ostringstream stream;
        stream << path << " must contain at least one instance.";
        return SetError(stream.str(), error_message);
    }

    output->clear();
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator)
    {
        GameConfig config;
        const std::string instance_path = JoinPath(path, iterator.key());
        if (!ParseGameConfig(iterator.value(), iterator.key(), &config, instance_path, error_message))
        {
            return false;
        }

        output->emplace(iterator.key(), std::move(config));
    }

    ClearError(error_message);
    return true;
}

} // namespace

std::string_view ConfigErrorMessage(ConfigErrorCode code) noexcept
{
    switch (code)
    {
    case ConfigErrorCode::None:
        return "No error.";
    case ConfigErrorCode::InvalidArgument:
        return "Invalid config API argument.";
    case ConfigErrorCode::JsonLoadFailed:
        return "Failed to load JSON configuration file.";
    case ConfigErrorCode::ExpectedObject:
        return "Configuration value must be an object.";
    case ConfigErrorCode::UnknownField:
        return "Configuration contains an unknown field.";
    case ConfigErrorCode::MissingRequiredField:
        return "Configuration is missing a required field.";
    case ConfigErrorCode::InvalidString:
        return "Configuration value must be a string.";
    case ConfigErrorCode::EmptyString:
        return "Configuration string value must not be empty.";
    case ConfigErrorCode::InvalidBoolean:
        return "Configuration value must be a boolean.";
    case ConfigErrorCode::InvalidUnsignedInteger:
        return "Configuration value must be an unsigned integer.";
    case ConfigErrorCode::ValueOutOfRange:
        return "Configuration value is out of range.";
    case ConfigErrorCode::InvalidLogLevel:
        return "Configuration log level is invalid.";
    case ConfigErrorCode::InvalidEndpointPort:
        return "Configuration endpoint port is invalid.";
    case ConfigErrorCode::InvalidNodeId:
        return "Node ID is invalid.";
    case ConfigErrorCode::MissingInstance:
        return "Selected node instance is missing.";
    case ConfigErrorCode::EmptyCollection:
        return "Configuration collection must not be empty.";
    case ConfigErrorCode::LoggingConfigInvalid:
        return "Logging configuration is invalid.";
    case ConfigErrorCode::Unknown:
        return "Unknown configuration error.";
    }

    return "Unknown configuration error.";
}

ConfigErrorCode LoadClusterConfigFile(
    const std::filesystem::path& path,
    ClusterConfig* output,
    std::string* error_message)
{
    static constexpr std::array<std::string_view, 7> kAllowedRootFields{
        "env",
        "logging",
        "kcp",
        "managed",
        "gm",
        "gate",
        "game",
    };

    if (output == nullptr)
    {
        return SetConfigError(ConfigErrorCode::InvalidArgument, "Cluster config output must not be null.", error_message);
    }

    Json document;
    std::string detail_error;
    const JsonErrorCode load_result = TryLoadJsonFile(path, &document, &detail_error);
    if (load_result != JsonErrorCode::None)
    {
        return SetConfigError(ConfigErrorCode::JsonLoadFailed, std::move(detail_error), error_message);
    }

    if (!ExpectObject(document, "root", &detail_error))
    {
        const ConfigErrorCode classified_error = ClassifyConfigError(detail_error);
        return SetConfigError(classified_error, std::move(detail_error), error_message);
    }

    if (!RejectUnknownFields(document, kAllowedRootFields, "root", &detail_error))
    {
        const ConfigErrorCode classified_error = ClassifyConfigError(detail_error);
        return SetConfigError(classified_error, std::move(detail_error), error_message);
    }

    const Json* env = nullptr;
    const Json* logging = nullptr;
    const Json* kcp = nullptr;
    const Json* managed = nullptr;
    const Json* gm = nullptr;
    const Json* gate = nullptr;
    const Json* game = nullptr;
    if (!GetRequiredMember(document, "env", &env, "root", &detail_error) ||
        !GetRequiredMember(document, "logging", &logging, "root", &detail_error) ||
        !GetRequiredMember(document, "kcp", &kcp, "root", &detail_error) ||
        !GetRequiredMember(document, "managed", &managed, "root", &detail_error) ||
        !GetRequiredMember(document, "gm", &gm, "root", &detail_error) ||
        !GetRequiredMember(document, "gate", &gate, "root", &detail_error) ||
        !GetRequiredMember(document, "game", &game, "root", &detail_error))
    {
        const ConfigErrorCode classified_error = ClassifyConfigError(detail_error);
        return SetConfigError(classified_error, std::move(detail_error), error_message);
    }

    ClusterConfig cluster_config;
    if (!ParseEnvConfig(*env, &cluster_config.env, "env", &detail_error) ||
        !ParseLoggingBlock(*logging, LoggingConfig{}, &cluster_config.logging, "logging", &detail_error) ||
        !ParseKcpConfig(*kcp, &cluster_config.kcp, "kcp", &detail_error) ||
        !ParseManagedConfig(*managed, &cluster_config.managed, path.parent_path(), "managed", &detail_error) ||
        !ParseGmConfig(*gm, &cluster_config.gm, "gm", &detail_error) ||
        !ParseGateCollection(*gate, &cluster_config.gates, "gate", &detail_error) ||
        !ParseGameCollection(*game, &cluster_config.games, "game", &detail_error))
    {
        const ConfigErrorCode classified_error = ClassifyConfigError(detail_error);
        return SetConfigError(classified_error, std::move(detail_error), error_message);
    }

    *output = std::move(cluster_config);
    ClearError(error_message);
    return ConfigErrorCode::None;
}

ConfigErrorCode SelectNodeConfig(
    const ClusterConfig& cluster_config,
    std::string_view node_id,
    std::unique_ptr<NodeConfig>* output,
    std::string* error_message)
{
    if (output == nullptr)
    {
        return SetConfigError(ConfigErrorCode::InvalidArgument, "Node config output must not be null.", error_message);
    }

    *output = nullptr;

    const auto parsed_node_id = ParseNodeId(node_id);
    if (!parsed_node_id.has_value())
    {
        return SetConfigError(
            ConfigErrorCode::InvalidNodeId,
            "nodeId must be one of GM, Gate<index>, or Game<index>.",
            error_message);
    }

    switch (parsed_node_id->kind)
    {
    case ParsedNodeKind::Gm:
    {
        auto node_config = std::make_unique<GmNodeConfig>();
        node_config->inner_network_listen_endpoint = cluster_config.gm.inner_network_listen_endpoint;
        node_config->control_network_listen_endpoint = cluster_config.gm.control_network_listen_endpoint;
        *output = std::move(node_config);
        break;
    }

    case ParsedNodeKind::Gate:
    {
        const auto iterator = cluster_config.gates.find(node_id);
        if (iterator == cluster_config.gates.end())
        {
            std::ostringstream stream;
            stream << "Missing gate instance for nodeId '" << node_id << "'.";
            return SetConfigError(ConfigErrorCode::MissingInstance, stream.str(), error_message);
        }

        auto node_config = std::make_unique<GateNodeConfig>();
        node_config->inner_network_listen_endpoint = iterator->second.inner_network_listen_endpoint;
        node_config->client_network_listen_endpoint = iterator->second.client_network_listen_endpoint;
        *output = std::move(node_config);
        break;
    }

    case ParsedNodeKind::Game:
    {
        const auto iterator = cluster_config.games.find(node_id);
        if (iterator == cluster_config.games.end())
        {
            std::ostringstream stream;
            stream << "Missing game instance for nodeId '" << node_id << "'.";
            return SetConfigError(ConfigErrorCode::MissingInstance, stream.str(), error_message);
        }

        auto node_config = std::make_unique<GameNodeConfig>();
        node_config->inner_network_listen_endpoint = iterator->second.inner_network_listen_endpoint;
        node_config->managed = cluster_config.managed;
        *output = std::move(node_config);
        break;
    }
    }

    ClearError(error_message);
    return ConfigErrorCode::None;
}

} // namespace xs::core

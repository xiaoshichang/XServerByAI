#include "Config.h"
#include "Json.h"

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace xs::core {
namespace {

void ClearError(std::string* error_message) {
    if (error_message != nullptr) {
        error_message->clear();
    }
}

bool SetError(std::string message, std::string* error_message) {
    if (error_message != nullptr) {
        *error_message = std::move(message);
    }
    return false;
}

std::string JoinPath(std::string_view parent, std::string_view child) {
    if (parent.empty()) {
        return std::string(child);
    }

    std::string path{parent};
    path.push_back('.');
    path.append(child);
    return path;
}

bool HasDigitsSuffix(std::string_view value, std::size_t prefix_length) noexcept {
    if (value.size() <= prefix_length) {
        return false;
    }

    for (std::size_t index = prefix_length; index < value.size(); ++index) {
        const char ch = value[index];
        if (ch < '0' || ch > '9') {
            return false;
        }
    }

    return true;
}

template <std::size_t N>
bool RejectUnknownFields(
    const Json& object,
    const std::array<std::string_view, N>& allowed_fields,
    std::string_view path,
    std::string* error_message) {
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        const std::string_view key = iterator.key();
        const auto match = std::find(allowed_fields.begin(), allowed_fields.end(), key);
        if (match == allowed_fields.end()) {
            std::ostringstream stream;
            stream << "Unknown field '" << iterator.key() << "' at " << path << '.';
            return SetError(stream.str(), error_message);
        }
    }

    return true;
}

bool ExpectObject(const Json& value, std::string_view path, std::string* error_message) {
    if (!value.is_object()) {
        std::ostringstream stream;
        stream << path << " must be an object.";
        return SetError(stream.str(), error_message);
    }

    return true;
}

bool TryGetMember(const Json& object, std::string_view key, const Json** member) {
    const auto iterator = object.find(std::string(key));
    if (iterator == object.end()) {
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
    std::string* error_message) {
    if (!TryGetMember(object, key, member)) {
        std::ostringstream stream;
        stream << "Missing required field '" << JoinPath(path, key) << "'.";
        return SetError(stream.str(), error_message);
    }

    return true;
}

bool ParseString(const Json& value, std::string_view path, std::string* output, std::string* error_message) {
    if (!value.is_string()) {
        std::ostringstream stream;
        stream << path << " must be a string.";
        return SetError(stream.str(), error_message);
    }

    *output = value.get<std::string>();
    return true;
}

bool ParseNonEmptyString(const Json& value, std::string_view path, std::string* output, std::string* error_message) {
    if (!ParseString(value, path, output, error_message)) {
        return false;
    }

    if (output->empty()) {
        std::ostringstream stream;
        stream << path << " must not be empty.";
        return SetError(stream.str(), error_message);
    }

    return true;
}

bool ParseBool(const Json& value, std::string_view path, bool* output, std::string* error_message) {
    if (!value.is_boolean()) {
        std::ostringstream stream;
        stream << path << " must be a boolean.";
        return SetError(stream.str(), error_message);
    }

    *output = value.get<bool>();
    return true;
}

template <typename T>
bool ParseUnsignedInteger(const Json& value, std::string_view path, T* output, std::string* error_message) {
    static_assert(std::numeric_limits<T>::is_integer, "T must be an integer type.");
    static_assert(!std::numeric_limits<T>::is_signed, "T must be unsigned.");

    std::uint64_t parsed_value = 0U;
    if (value.is_number_unsigned()) {
        parsed_value = value.get<std::uint64_t>();
    } else if (value.is_number_integer()) {
        const auto signed_value = value.get<std::int64_t>();
        if (signed_value < 0) {
            std::ostringstream stream;
            stream << path << " must be an unsigned integer.";
            return SetError(stream.str(), error_message);
        }

        parsed_value = static_cast<std::uint64_t>(signed_value);
    } else {
        std::ostringstream stream;
        stream << path << " must be an unsigned integer.";
        return SetError(stream.str(), error_message);
    }

    if (parsed_value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
        std::ostringstream stream;
        stream << path << " is out of range.";
        return SetError(stream.str(), error_message);
    }

    *output = static_cast<T>(parsed_value);
    return true;
}

bool ValidateLoggingConfigAtPath(const LoggingConfig& config, std::string_view path, std::string* error_message) {
    std::string validation_error;
    if (ValidateLoggingConfig(config, &validation_error)) {
        ClearError(error_message);
        return true;
    }

    if (validation_error.rfind("logging.", 0) == 0) {
        validation_error.replace(0, 8, std::string(path) + '.');
    } else {
        validation_error = std::string(path) + ": " + validation_error;
    }

    return SetError(std::move(validation_error), error_message);
}

bool ParseLoggingBlock(
    const Json& value,
    const LoggingConfig& base_config,
    LoggingConfig* output,
    std::string_view path,
    std::string* error_message) {
    static constexpr std::array<std::string_view, 6> kAllowedFields{
        "rootDir",
        "minLevel",
        "flushIntervalMs",
        "rotateDaily",
        "maxFileSizeMB",
        "maxRetainedFiles",
    };

    if (output == nullptr) {
        return SetError("Logging config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    LoggingConfig config = base_config;
    const Json* member = nullptr;

    if (TryGetMember(value, "rootDir", &member)) {
        if (!ParseString(*member, JoinPath(path, "rootDir"), &config.root_dir, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "minLevel", &member)) {
        std::string level_name;
        const std::string level_path = JoinPath(path, "minLevel");
        if (!ParseNonEmptyString(*member, level_path, &level_name, error_message)) {
            return false;
        }

        const auto parsed_level = ParseLogLevel(level_name);
        if (!parsed_level.has_value()) {
            std::ostringstream stream;
            stream << level_path << " must be one of Trace, Debug, Info, Warn, Error, Fatal.";
            return SetError(stream.str(), error_message);
        }

        config.min_level = *parsed_level;
    }

    if (TryGetMember(value, "flushIntervalMs", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "flushIntervalMs"), &config.flush_interval_ms, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "rotateDaily", &member)) {
        if (!ParseBool(*member, JoinPath(path, "rotateDaily"), &config.rotate_daily, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "maxFileSizeMB", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "maxFileSizeMB"), &config.max_file_size_mb, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "maxRetainedFiles", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "maxRetainedFiles"), &config.max_retained_files, error_message)) {
            return false;
        }
    }

    if (!ValidateLoggingConfigAtPath(config, path, error_message)) {
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
    std::string* error_message) {
    static constexpr std::array<std::string_view, 2> kAllowedFields{
        "host",
        "port",
    };

    if (output == nullptr) {
        return SetError("Endpoint config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    const Json* host_value = nullptr;
    const Json* port_value = nullptr;
    if (!GetRequiredMember(value, "host", &host_value, path, error_message) ||
        !GetRequiredMember(value, "port", &port_value, path, error_message)) {
        return false;
    }

    EndpointConfig endpoint;
    if (!ParseNonEmptyString(*host_value, JoinPath(path, "host"), &endpoint.host, error_message)) {
        return false;
    }

    std::uint32_t port = 0U;
    if (!ParseUnsignedInteger(*port_value, JoinPath(path, "port"), &port, error_message)) {
        return false;
    }

    if (port == 0U || port > std::numeric_limits<std::uint16_t>::max()) {
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
    std::string* error_message) {
    static constexpr std::array<std::string_view, 1> kAllowedFields{
        "listenEndpoint",
    };

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    const Json* endpoint_value = nullptr;
    if (!GetRequiredMember(value, "listenEndpoint", &endpoint_value, path, error_message)) {
        return false;
    }

    return ParseEndpointConfig(*endpoint_value, output, JoinPath(path, "listenEndpoint"), error_message);
}

bool ParseServerGroupConfig(
    const Json& value,
    ServerGroupConfig* output,
    std::string_view path,
    std::string* error_message) {
    static constexpr std::array<std::string_view, 2> kAllowedFields{
        "id",
        "environment",
    };

    if (output == nullptr) {
        return SetError("Server group output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    const Json* id_value = nullptr;
    const Json* environment_value = nullptr;
    if (!GetRequiredMember(value, "id", &id_value, path, error_message) ||
        !GetRequiredMember(value, "environment", &environment_value, path, error_message)) {
        return false;
    }

    ServerGroupConfig config;
    if (!ParseNonEmptyString(*id_value, JoinPath(path, "id"), &config.id, error_message) ||
        !ParseNonEmptyString(*environment_value, JoinPath(path, "environment"), &config.environment, error_message)) {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ValidateKcpConfig(const KcpConfig& config, std::string_view path, std::string* error_message) {
    const auto fail_positive = [path, error_message](std::string_view key) {
        std::ostringstream stream;
        stream << JoinPath(path, key) << " must be greater than zero.";
        return SetError(stream.str(), error_message);
    };

    if (config.mtu == 0U) {
        return fail_positive("mtu");
    }

    if (config.sndwnd == 0U) {
        return fail_positive("sndwnd");
    }

    if (config.rcvwnd == 0U) {
        return fail_positive("rcvwnd");
    }

    if (config.interval_ms == 0U) {
        return fail_positive("intervalMs");
    }

    if (config.min_rto_ms == 0U) {
        return fail_positive("minRtoMs");
    }

    if (config.dead_link_count == 0U) {
        return fail_positive("deadLinkCount");
    }

    ClearError(error_message);
    return true;
}

bool ParseKcpConfig(const Json& value, KcpConfig* output, std::string_view path, std::string* error_message) {
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

    if (output == nullptr) {
        return SetError("KCP config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    KcpConfig config = *output;
    const Json* member = nullptr;

    if (TryGetMember(value, "mtu", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "mtu"), &config.mtu, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "sndwnd", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "sndwnd"), &config.sndwnd, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "rcvwnd", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "rcvwnd"), &config.rcvwnd, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "nodelay", &member)) {
        if (!ParseBool(*member, JoinPath(path, "nodelay"), &config.nodelay, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "intervalMs", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "intervalMs"), &config.interval_ms, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "fastResend", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "fastResend"), &config.fast_resend, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "noCongestionWindow", &member)) {
        if (!ParseBool(*member, JoinPath(path, "noCongestionWindow"), &config.no_congestion_window, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "minRtoMs", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "minRtoMs"), &config.min_rto_ms, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "deadLinkCount", &member)) {
        if (!ParseUnsignedInteger(*member, JoinPath(path, "deadLinkCount"), &config.dead_link_count, error_message)) {
            return false;
        }
    }

    if (TryGetMember(value, "streamMode", &member)) {
        if (!ParseBool(*member, JoinPath(path, "streamMode"), &config.stream_mode, error_message)) {
            return false;
        }
    }

    if (!ValidateKcpConfig(config, path, error_message)) {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseManagedConfig(const Json& value, ManagedConfig* output, std::string_view path, std::string* error_message) {
    static constexpr std::array<std::string_view, 1> kAllowedFields{
        "assemblyName",
    };

    if (output == nullptr) {
        return SetError("Managed config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    ManagedConfig config = *output;
    const Json* assembly_name = nullptr;
    if (TryGetMember(value, "assemblyName", &assembly_name)) {
        if (!ParseNonEmptyString(*assembly_name, JoinPath(path, "assemblyName"), &config.assembly_name, error_message)) {
            return false;
        }
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseGmConfig(
    const Json& value,
    const LoggingConfig& default_logging,
    GmConfig* output,
    std::string_view path,
    std::string* error_message) {
    static constexpr std::array<std::string_view, 2> kAllowedFields{
        "control",
        "logging",
    };

    if (output == nullptr) {
        return SetError("GM config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    GmConfig config;
    config.logging = default_logging;

    const Json* logging = nullptr;
    if (TryGetMember(value, "logging", &logging)) {
        if (!ParseLoggingBlock(*logging, default_logging, &config.logging, JoinPath(path, "logging"), error_message)) {
            return false;
        }
    }

    const Json* control = nullptr;
    if (!GetRequiredMember(value, "control", &control, path, error_message)) {
        return false;
    }

    if (!ParseListenEndpointContainer(*control, &config.control_listen_endpoint, JoinPath(path, "control"), error_message)) {
        return false;
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseGateConfig(
    const Json& value,
    std::string_view selector,
    const LoggingConfig& default_logging,
    GateConfig* output,
    std::string_view path,
    std::string* error_message) {
    static constexpr std::array<std::string_view, 4> kAllowedFields{
        "nodeId",
        "service",
        "kcp",
        "logging",
    };

    if (output == nullptr) {
        return SetError("Gate config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    const auto parsed_selector = ParseNodeSelector(selector);
    if (!parsed_selector.has_value() || parsed_selector->kind != NodeSelectorKind::Gate) {
        std::ostringstream stream;
        stream << path << " has invalid selector '" << selector << "'.";
        return SetError(stream.str(), error_message);
    }

    GateConfig config;
    config.selector = std::string(selector);
    config.logging = default_logging;

    const Json* logging = nullptr;
    if (TryGetMember(value, "logging", &logging)) {
        if (!ParseLoggingBlock(*logging, default_logging, &config.logging, JoinPath(path, "logging"), error_message)) {
            return false;
        }
    }

    const Json* node_id = nullptr;
    const Json* service = nullptr;
    if (!GetRequiredMember(value, "nodeId", &node_id, path, error_message) ||
        !GetRequiredMember(value, "service", &service, path, error_message)) {
        return false;
    }

    if (!ParseNonEmptyString(*node_id, JoinPath(path, "nodeId"), &config.node_id, error_message)) {
        return false;
    }

    const std::string expected_node_id = SelectorCanonicalNodeId(*parsed_selector);
    if (config.node_id != expected_node_id) {
        std::ostringstream stream;
        stream << JoinPath(path, "nodeId") << " must equal '" << expected_node_id << "'.";
        return SetError(stream.str(), error_message);
    }

    if (!ParseListenEndpointContainer(*service, &config.service_listen_endpoint, JoinPath(path, "service"), error_message)) {
        return false;
    }

    const Json* kcp = nullptr;
    if (TryGetMember(value, "kcp", &kcp)) {
        if (!ParseKcpConfig(*kcp, &config.kcp, JoinPath(path, "kcp"), error_message)) {
            return false;
        }
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseGameConfig(
    const Json& value,
    std::string_view selector,
    const LoggingConfig& default_logging,
    GameConfig* output,
    std::string_view path,
    std::string* error_message) {
    static constexpr std::array<std::string_view, 4> kAllowedFields{
        "nodeId",
        "service",
        "managed",
        "logging",
    };

    if (output == nullptr) {
        return SetError("Game config output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (!RejectUnknownFields(value, kAllowedFields, path, error_message)) {
        return false;
    }

    const auto parsed_selector = ParseNodeSelector(selector);
    if (!parsed_selector.has_value() || parsed_selector->kind != NodeSelectorKind::Game) {
        std::ostringstream stream;
        stream << path << " has invalid selector '" << selector << "'.";
        return SetError(stream.str(), error_message);
    }

    GameConfig config;
    config.selector = std::string(selector);
    config.logging = default_logging;

    const Json* logging = nullptr;
    if (TryGetMember(value, "logging", &logging)) {
        if (!ParseLoggingBlock(*logging, default_logging, &config.logging, JoinPath(path, "logging"), error_message)) {
            return false;
        }
    }

    const Json* node_id = nullptr;
    const Json* service = nullptr;
    if (!GetRequiredMember(value, "nodeId", &node_id, path, error_message) ||
        !GetRequiredMember(value, "service", &service, path, error_message)) {
        return false;
    }

    if (!ParseNonEmptyString(*node_id, JoinPath(path, "nodeId"), &config.node_id, error_message)) {
        return false;
    }

    const std::string expected_node_id = SelectorCanonicalNodeId(*parsed_selector);
    if (config.node_id != expected_node_id) {
        std::ostringstream stream;
        stream << JoinPath(path, "nodeId") << " must equal '" << expected_node_id << "'.";
        return SetError(stream.str(), error_message);
    }

    if (!ParseListenEndpointContainer(*service, &config.service_listen_endpoint, JoinPath(path, "service"), error_message)) {
        return false;
    }

    const Json* managed = nullptr;
    if (TryGetMember(value, "managed", &managed)) {
        if (!ParseManagedConfig(*managed, &config.managed, JoinPath(path, "managed"), error_message)) {
            return false;
        }
    }

    *output = std::move(config);
    ClearError(error_message);
    return true;
}

bool ParseGateCollection(
    const Json& value,
    const LoggingConfig& default_logging,
    std::map<std::string, GateConfig, std::less<>>* output,
    std::string_view path,
    std::string* error_message) {
    if (output == nullptr) {
        return SetError("Gate collection output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (value.empty()) {
        std::ostringstream stream;
        stream << path << " must contain at least one instance.";
        return SetError(stream.str(), error_message);
    }

    output->clear();
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
        GateConfig config;
        const std::string instance_path = JoinPath(path, iterator.key());
        if (!ParseGateConfig(iterator.value(), iterator.key(), default_logging, &config, instance_path, error_message)) {
            return false;
        }

        output->emplace(iterator.key(), std::move(config));
    }

    ClearError(error_message);
    return true;
}

bool ParseGameCollection(
    const Json& value,
    const LoggingConfig& default_logging,
    std::map<std::string, GameConfig, std::less<>>* output,
    std::string_view path,
    std::string* error_message) {
    if (output == nullptr) {
        return SetError("Game collection output must not be null.", error_message);
    }

    if (!ExpectObject(value, path, error_message)) {
        return false;
    }

    if (value.empty()) {
        std::ostringstream stream;
        stream << path << " must contain at least one instance.";
        return SetError(stream.str(), error_message);
    }

    output->clear();
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
        GameConfig config;
        const std::string instance_path = JoinPath(path, iterator.key());
        if (!ParseGameConfig(iterator.value(), iterator.key(), default_logging, &config, instance_path, error_message)) {
            return false;
        }

        output->emplace(iterator.key(), std::move(config));
    }

    ClearError(error_message);
    return true;
}

} // namespace

std::optional<NodeSelector> ParseNodeSelector(std::string_view selector) noexcept {
    if (selector == "gm") {
        return NodeSelector{NodeSelectorKind::Gm, std::string(selector)};
    }

    if (selector.starts_with("gate") && HasDigitsSuffix(selector, 4U)) {
        return NodeSelector{NodeSelectorKind::Gate, std::string(selector)};
    }

    if (selector.starts_with("game") && HasDigitsSuffix(selector, 4U)) {
        return NodeSelector{NodeSelectorKind::Game, std::string(selector)};
    }

    return std::nullopt;
}

std::string SelectorCanonicalNodeId(const NodeSelector& selector) {
    switch (selector.kind) {
    case NodeSelectorKind::Gm:
        return "GM";
    case NodeSelectorKind::Gate:
        return "Gate" + selector.value.substr(4);
    case NodeSelectorKind::Game:
        return "Game" + selector.value.substr(4);
    }

    return "GM";
}

bool LoadClusterConfigFile(
    const std::filesystem::path& path,
    ClusterConfig* output,
    std::string* error_message) {
    static constexpr std::array<std::string_view, 5> kAllowedRootFields{
        "serverGroup",
        "logging",
        "gm",
        "gate",
        "game",
    };

    if (output == nullptr) {
        return SetError("Cluster config output must not be null.", error_message);
    }

    Json document;
    if (!TryLoadJsonFile(path, &document, error_message)) {
        return false;
    }

    if (!ExpectObject(document, "root", error_message)) {
        return false;
    }

    if (!RejectUnknownFields(document, kAllowedRootFields, "root", error_message)) {
        return false;
    }

    const Json* server_group = nullptr;
    const Json* logging = nullptr;
    const Json* gm = nullptr;
    const Json* gate = nullptr;
    const Json* game = nullptr;
    if (!GetRequiredMember(document, "serverGroup", &server_group, "root", error_message) ||
        !GetRequiredMember(document, "logging", &logging, "root", error_message) ||
        !GetRequiredMember(document, "gm", &gm, "root", error_message) ||
        !GetRequiredMember(document, "gate", &gate, "root", error_message) ||
        !GetRequiredMember(document, "game", &game, "root", error_message)) {
        return false;
    }

    ClusterConfig cluster_config;
    if (!ParseServerGroupConfig(*server_group, &cluster_config.server_group, "serverGroup", error_message) ||
        !ParseLoggingBlock(*logging, LoggingConfig{}, &cluster_config.logging_defaults, "logging", error_message) ||
        !ParseGmConfig(*gm, cluster_config.logging_defaults, &cluster_config.gm, "gm", error_message) ||
        !ParseGateCollection(*gate, cluster_config.logging_defaults, &cluster_config.gates, "gate", error_message) ||
        !ParseGameCollection(*game, cluster_config.logging_defaults, &cluster_config.games, "game", error_message)) {
        return false;
    }

    *output = std::move(cluster_config);
    ClearError(error_message);
    return true;
}

bool SelectNodeConfig(
    const ClusterConfig& cluster_config,
    std::string_view selector,
    NodeConfig* output,
    std::string* error_message) {
    if (output == nullptr) {
        return SetError("Node config output must not be null.", error_message);
    }

    const auto parsed_selector = ParseNodeSelector(selector);
    if (!parsed_selector.has_value()) {
        return SetError("selector must be one of gm, gate<index>, or game<index>.", error_message);
    }

    NodeConfig node_config;
    node_config.selector = parsed_selector->value;
    node_config.server_group = cluster_config.server_group;

    switch (parsed_selector->kind) {
    case NodeSelectorKind::Gm:
        node_config.process_type = ProcessType::Gm;
        node_config.instance_id = "GM";
        node_config.logging = cluster_config.gm.logging;
        node_config.control_listen_endpoint = cluster_config.gm.control_listen_endpoint;
        break;

    case NodeSelectorKind::Gate: {
        const auto iterator = cluster_config.gates.find(parsed_selector->value);
        if (iterator == cluster_config.gates.end()) {
            std::ostringstream stream;
            stream << "Missing gate instance for selector '" << parsed_selector->value << "'.";
            return SetError(stream.str(), error_message);
        }

        node_config.process_type = ProcessType::Gate;
        node_config.instance_id = iterator->second.node_id;
        node_config.logging = iterator->second.logging;
        node_config.service_listen_endpoint = iterator->second.service_listen_endpoint;
        node_config.kcp = iterator->second.kcp;
        break;
    }

    case NodeSelectorKind::Game: {
        const auto iterator = cluster_config.games.find(parsed_selector->value);
        if (iterator == cluster_config.games.end()) {
            std::ostringstream stream;
            stream << "Missing game instance for selector '" << parsed_selector->value << "'.";
            return SetError(stream.str(), error_message);
        }

        node_config.process_type = ProcessType::Game;
        node_config.instance_id = iterator->second.node_id;
        node_config.logging = iterator->second.logging;
        node_config.service_listen_endpoint = iterator->second.service_listen_endpoint;
        node_config.managed = iterator->second.managed;
        break;
    }
    }

    *output = std::move(node_config);
    ClearError(error_message);
    return true;
}

bool LoadNodeConfigFile(
    const std::filesystem::path& path,
    std::string_view selector,
    NodeConfig* output,
    std::string* error_message) {
    if (output == nullptr) {
        return SetError("Node config output must not be null.", error_message);
    }

    ClusterConfig cluster_config;
    if (!LoadClusterConfigFile(path, &cluster_config, error_message)) {
        return false;
    }

    if (!SelectNodeConfig(cluster_config, selector, output, error_message)) {
        return false;
    }

    output->source_path = path;
    ClearError(error_message);
    return true;
}

} // namespace xs::core

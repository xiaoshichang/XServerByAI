#include <iostream>
#include <optional>
#include <string_view>

namespace {

enum class NodeSelectorKind {
    Gm,
    Gate,
    Game,
};

bool HasDigitsSuffix(std::string_view value, std::size_t prefix_length) {
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

std::optional<NodeSelectorKind> ParseSelector(std::string_view selector) {
    if (selector == "gm") {
        return NodeSelectorKind::Gm;
    }

    if (selector.starts_with("gate") && HasDigitsSuffix(selector, 4)) {
        return NodeSelectorKind::Gate;
    }

    if (selector.starts_with("game") && HasDigitsSuffix(selector, 4)) {
        return NodeSelectorKind::Game;
    }

    return std::nullopt;
}

void PrintUsage() {
    std::cerr << "Usage: xserver-node <configPath> <gm|gateN|gameN>\n";
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        PrintUsage();
        return 1;
    }

    const std::string_view config_path = argv[1];
    const std::string_view selector = argv[2];

    if (config_path.empty() || !ParseSelector(selector).has_value()) {
        PrintUsage();
        return 1;
    }

    return 0;
}

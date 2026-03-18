#include "Config.h"

#include <iostream>
#include <string_view>

namespace
{

void PrintUsage()
{
    std::cerr << "Usage: xserver-node <configPath> <gm|gateN|gameN>\n";
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        PrintUsage();
        return 1;
    }

    const std::string_view config_path = argv[1];
    const std::string_view selector = argv[2];
    if (config_path.empty())
    {
        PrintUsage();
        return 1;
    }

    xs::core::NodeConfig node_config;
    std::string error_message;
    const xs::core::ConfigErrorCode load_result =
        xs::core::LoadNodeConfigFile(config_path, selector, &node_config, &error_message);
    if (load_result != xs::core::ConfigErrorCode::None)
    {
        if (error_message.empty())
        {
            error_message = std::string(xs::core::ConfigErrorMessage(load_result));
        }

        std::cerr << error_message << '\n';
        return 1;
    }

    return 0;
}

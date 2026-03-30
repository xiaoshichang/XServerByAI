#pragma once

#include "Json.h"

#include <filesystem>
#include <string>

#ifndef XS_TEST_MANAGED_FRAMEWORK_ASSEMBLY_PATH
#error XS_TEST_MANAGED_FRAMEWORK_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_MANAGED_FRAMEWORK_RUNTIMECONFIG_PATH
#error XS_TEST_MANAGED_FRAMEWORK_RUNTIMECONFIG_PATH must be defined.
#endif

#ifndef XS_TEST_MANAGED_GAMELOGIC_ASSEMBLY_PATH
#error XS_TEST_MANAGED_GAMELOGIC_ASSEMBLY_PATH must be defined.
#endif

namespace xs::tests
{

inline xs::core::Json MakeManagedConfigJson()
{
    const std::string framework_assembly_path =
        std::filesystem::path{XS_TEST_MANAGED_FRAMEWORK_ASSEMBLY_PATH}.lexically_normal().string();
    const std::string framework_runtime_config_path =
        std::filesystem::path{XS_TEST_MANAGED_FRAMEWORK_RUNTIMECONFIG_PATH}.lexically_normal().string();
    const std::string game_logic_assembly_path =
        std::filesystem::path{XS_TEST_MANAGED_GAMELOGIC_ASSEMBLY_PATH}.lexically_normal().string();

    return xs::core::Json{
        {"assemblyName", "XServer.Managed.Framework"},
        {"assemblyPath", framework_assembly_path},
        {"runtimeConfigPath", framework_runtime_config_path},
        {"searchAssemblyPaths", xs::core::Json::array({framework_assembly_path, game_logic_assembly_path})},
    };
}

} // namespace xs::tests

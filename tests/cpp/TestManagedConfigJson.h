#pragma once

#include "Json.h"

#include <filesystem>

#ifndef XS_TEST_GAMELOGIC_ASSEMBLY_PATH
#error XS_TEST_GAMELOGIC_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH
#error XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH must be defined.
#endif

namespace xs::tests
{

inline xs::core::Json MakeManagedConfigJson()
{
    return xs::core::Json{
        {"assemblyName", "XServer.Managed.GameLogic"},
        {"assemblyPath", std::filesystem::path{XS_TEST_GAMELOGIC_ASSEMBLY_PATH}.lexically_normal().string()},
        {"runtimeConfigPath",
         std::filesystem::path{XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH}.lexically_normal().string()},
    };
}

} // namespace xs::tests

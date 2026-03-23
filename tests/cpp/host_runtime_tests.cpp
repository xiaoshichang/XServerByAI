#include "ManagedRuntimeHost.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef XS_TEST_GAMELOGIC_ASSEMBLY_PATH
#error XS_TEST_GAMELOGIC_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH
#error XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH must be defined.
#endif

namespace
{

int g_failures = 0;

void Check(bool condition, const char* expression, const char* message = nullptr)
{
    if (condition)
    {
        return;
    }

    std::cerr << "Check failed: " << expression;
    if (message != nullptr)
    {
        std::cerr << " (" << message << ")";
    }
    std::cerr << '\n';
    ++g_failures;
}

#define XS_CHECK(expr) Check((expr), #expr)
#define XS_CHECK_MSG(expr, message) Check((expr), #expr, (message))

const std::filesystem::path kManagedAssemblyPath{XS_TEST_GAMELOGIC_ASSEMBLY_PATH};
const std::filesystem::path kManagedRuntimeConfigPath{XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH};

std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code error_code;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, error_code);
    if (!error_code)
    {
        return canonical;
    }

    const std::filesystem::path absolute = std::filesystem::absolute(path, error_code);
    if (!error_code)
    {
        return absolute;
    }

    return path;
}

void TestManagedAssetsExist()
{
    XS_CHECK_MSG(std::filesystem::exists(kManagedAssemblyPath), kManagedAssemblyPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kManagedRuntimeConfigPath), kManagedRuntimeConfigPath.string().c_str());
}

void TestManagedHostErrorMetadata()
{
    XS_CHECK(
        xs::host::ManagedHostErrorCanonicalName(xs::host::ManagedHostErrorCode::RuntimeInitializeFailed) ==
        std::string_view("Interop.RuntimeInitializeFailed"));
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(xs::host::ManagedHostErrorCode::RuntimeDelegateLoadFailed) ==
        std::string_view("Failed to resolve load_assembly_and_get_function_pointer from hostfxr."));
}

void TestLoadRejectsMissingRuntimeConfig()
{
    xs::host::ManagedRuntimeHost host;
    const std::filesystem::path missing_runtime_config =
        std::filesystem::current_path() / "test-output" / "host-runtime-tests" / "missing.runtimeconfig.json";

    std::string error_message;
    const xs::host::ManagedHostErrorCode result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            missing_runtime_config,
            kManagedAssemblyPath,
        },
        &error_message);

    XS_CHECK(result == xs::host::ManagedHostErrorCode::RuntimeConfigPathNotFound);
    XS_CHECK(!error_message.empty());
    XS_CHECK_MSG(
        error_message.find("Managed runtime config was not found") != std::string::npos,
        error_message.c_str());
    XS_CHECK(!host.IsLoaded());
}

void TestLoadRejectsMissingAssembly()
{
    xs::host::ManagedRuntimeHost host;
    const std::filesystem::path missing_assembly =
        std::filesystem::current_path() / "test-output" / "host-runtime-tests" / "missing-assembly.dll";

    std::string error_message;
    const xs::host::ManagedHostErrorCode result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kManagedRuntimeConfigPath,
            missing_assembly,
        },
        &error_message);

    XS_CHECK(result == xs::host::ManagedHostErrorCode::AssemblyPathNotFound);
    XS_CHECK(!error_message.empty());
    XS_CHECK_MSG(
        error_message.find("Managed root assembly was not found") != std::string::npos,
        error_message.c_str());
    XS_CHECK(!host.IsLoaded());
}

void TestLoadSucceedsAndExposesDelegate()
{
    xs::host::ManagedRuntimeHost host;
    std::string error_message;

    const xs::host::ManagedHostErrorCode load_result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kManagedRuntimeConfigPath,
            kManagedAssemblyPath,
        },
        &error_message);

    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, error_message.c_str());
    XS_CHECK(error_message.empty());
    XS_CHECK(host.IsLoaded());
    XS_CHECK(host.load_assembly_and_get_function_pointer() != nullptr);
    XS_CHECK(NormalizePath(host.runtime_config_path()) == NormalizePath(kManagedRuntimeConfigPath));
    XS_CHECK(NormalizePath(host.assembly_path()) == NormalizePath(kManagedAssemblyPath));
    XS_CHECK(!host.hostfxr_path().empty());
    XS_CHECK(host.hostfxr_path().filename().string().find("hostfxr") != std::string::npos);

    std::string second_error_message;
    const xs::host::ManagedHostErrorCode second_load_result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kManagedRuntimeConfigPath,
            kManagedAssemblyPath,
        },
        &second_error_message);

    XS_CHECK(second_load_result == xs::host::ManagedHostErrorCode::AlreadyLoaded);
    XS_CHECK(!second_error_message.empty());

    std::string unload_error_message;
    XS_CHECK(host.Unload(&unload_error_message) == xs::host::ManagedHostErrorCode::None);
    XS_CHECK(unload_error_message.empty());
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(host.load_assembly_and_get_function_pointer() == nullptr);
    XS_CHECK(host.runtime_config_path().empty());
    XS_CHECK(host.assembly_path().empty());
    XS_CHECK(host.hostfxr_path().empty());
}

} // namespace

int main()
{
    TestManagedAssetsExist();
    TestManagedHostErrorMetadata();
    TestLoadRejectsMissingRuntimeConfig();
    TestLoadRejectsMissingAssembly();
    TestLoadSucceedsAndExposesDelegate();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " managed host runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include "ManagedRuntimeHost.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#ifndef XS_TEST_GAMELOGIC_ASSEMBLY_PATH
#error XS_TEST_GAMELOGIC_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH
#error XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH must be defined.
#endif

#ifndef XS_TEST_GAMELOGIC_ABI_MISMATCH_ASSEMBLY_PATH
#error XS_TEST_GAMELOGIC_ABI_MISMATCH_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_GAMELOGIC_ABI_MISMATCH_RUNTIMECONFIG_PATH
#error XS_TEST_GAMELOGIC_ABI_MISMATCH_RUNTIMECONFIG_PATH must be defined.
#endif

#ifndef XS_TEST_GAMELOGIC_MISSING_EXPORTS_ASSEMBLY_PATH
#error XS_TEST_GAMELOGIC_MISSING_EXPORTS_ASSEMBLY_PATH must be defined.
#endif

#ifndef XS_TEST_GAMELOGIC_MISSING_EXPORTS_RUNTIMECONFIG_PATH
#error XS_TEST_GAMELOGIC_MISSING_EXPORTS_RUNTIMECONFIG_PATH must be defined.
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
const std::filesystem::path kAbiMismatchAssemblyPath{XS_TEST_GAMELOGIC_ABI_MISMATCH_ASSEMBLY_PATH};
const std::filesystem::path kAbiMismatchRuntimeConfigPath{XS_TEST_GAMELOGIC_ABI_MISMATCH_RUNTIMECONFIG_PATH};
const std::filesystem::path kMissingExportsAssemblyPath{XS_TEST_GAMELOGIC_MISSING_EXPORTS_ASSEMBLY_PATH};
const std::filesystem::path kMissingExportsRuntimeConfigPath{XS_TEST_GAMELOGIC_MISSING_EXPORTS_RUNTIMECONFIG_PATH};

std::string DescribeManagedHostResult(xs::host::ManagedHostErrorCode code)
{
    return std::string(xs::host::ManagedHostErrorCanonicalName(code)) + ": " +
           std::string(xs::host::ManagedHostErrorMessage(code));
}

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

std::string ReadManagedUtf8(const std::uint8_t* buffer, std::uint32_t length)
{
    if (buffer == nullptr)
    {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(length));
}

void TestManagedAssetsExist()
{
    XS_CHECK_MSG(std::filesystem::exists(kManagedAssemblyPath), kManagedAssemblyPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kManagedRuntimeConfigPath), kManagedRuntimeConfigPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kAbiMismatchAssemblyPath), kAbiMismatchAssemblyPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kAbiMismatchRuntimeConfigPath), kAbiMismatchRuntimeConfigPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kMissingExportsAssemblyPath), kMissingExportsAssemblyPath.string().c_str());
    XS_CHECK_MSG(std::filesystem::exists(kMissingExportsRuntimeConfigPath), kMissingExportsRuntimeConfigPath.string().c_str());
}

void TestManagedHostErrorMetadata()
{
    XS_CHECK(
        xs::host::ManagedHostErrorCanonicalName(xs::host::ManagedHostErrorCode::RuntimeInitializeFailed) ==
        std::string_view("Interop.RuntimeInitializeFailed"));
    XS_CHECK(
        xs::host::ManagedHostErrorCanonicalName(xs::host::ManagedHostErrorCode::AbiVersionMismatch) ==
        std::string_view("Interop.AbiVersionMismatch"));
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(xs::host::ManagedHostErrorCode::RuntimeDelegateLoadFailed) ==
        std::string_view("Failed to resolve load_assembly_and_get_function_pointer from hostfxr."));
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(xs::host::ManagedHostErrorCode::RuntimeNotLoaded) ==
        std::string_view("Managed runtime host must be loaded before binding game exports."));
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(xs::host::ManagedHostErrorCode::EntryPointNotBound) ==
        std::string_view("Managed game entry points have not been bound successfully."));
}

void TestBindRejectsRuntimeNotLoaded()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode result = host.BindGameExports();

    XS_CHECK(result == xs::host::ManagedHostErrorCode::RuntimeNotLoaded);
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(result) ==
        std::string_view("Managed runtime host must be loaded before binding game exports."));
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(!host.AreGameExportsBound());
}

void TestGetExportsRejectsUnboundHost()
{
    xs::host::ManagedRuntimeHost host;
    xs::host::ManagedGameExports exports{};

    const xs::host::ManagedHostErrorCode result = host.GetGameExports(exports);

    XS_CHECK(result == xs::host::ManagedHostErrorCode::EntryPointNotBound);
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(result) ==
        std::string_view("Managed game entry points have not been bound successfully."));
    XS_CHECK(!host.AreGameExportsBound());
}

void TestLoadRejectsMissingRuntimeConfig()
{
    xs::host::ManagedRuntimeHost host;
    const std::filesystem::path missing_runtime_config =
        std::filesystem::current_path() / "test-output" / "host-runtime-tests" / "missing.runtimeconfig.json";

    const xs::host::ManagedHostErrorCode result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            missing_runtime_config,
            kManagedAssemblyPath,
        });

    XS_CHECK(result == xs::host::ManagedHostErrorCode::RuntimeConfigPathNotFound);
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(result) ==
        std::string_view("Managed runtime config was not found."));
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(!host.AreGameExportsBound());
}

void TestLoadRejectsMissingAssembly()
{
    xs::host::ManagedRuntimeHost host;
    const std::filesystem::path missing_assembly =
        std::filesystem::current_path() / "test-output" / "host-runtime-tests" / "missing-assembly.dll";

    const xs::host::ManagedHostErrorCode result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kManagedRuntimeConfigPath,
            missing_assembly,
        });

    XS_CHECK(result == xs::host::ManagedHostErrorCode::AssemblyPathNotFound);
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(result) ==
        std::string_view("Managed root assembly was not found."));
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(!host.AreGameExportsBound());
}

void TestLoadAndBindGameExportsSucceed()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kManagedRuntimeConfigPath,
            kManagedAssemblyPath,
        });

    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());
    XS_CHECK(host.IsLoaded());
    XS_CHECK(!host.AreGameExportsBound());
    XS_CHECK(host.load_assembly_and_get_function_pointer() != nullptr);
    XS_CHECK(NormalizePath(host.runtime_config_path()) == NormalizePath(kManagedRuntimeConfigPath));
    XS_CHECK(NormalizePath(host.assembly_path()) == NormalizePath(kManagedAssemblyPath));
    XS_CHECK(!host.hostfxr_path().empty());
    XS_CHECK(host.hostfxr_path().filename().string().find("hostfxr") != std::string::npos);

    xs::host::ManagedGameExports exports_before_bind{};
    const xs::host::ManagedHostErrorCode unbound_result = host.GetGameExports(exports_before_bind);
    XS_CHECK(unbound_result == xs::host::ManagedHostErrorCode::EntryPointNotBound);

    const xs::host::ManagedHostErrorCode bind_result = host.BindGameExports();
    XS_CHECK_MSG(bind_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(bind_result).c_str());
    XS_CHECK(host.AreGameExportsBound());

    const xs::host::ManagedHostErrorCode second_bind_result = host.BindGameExports();
    XS_CHECK(second_bind_result == xs::host::ManagedHostErrorCode::None);

    xs::host::ManagedGameExports exports{};
    const xs::host::ManagedHostErrorCode get_exports_result = host.GetGameExports(exports);
    XS_CHECK_MSG(
        get_exports_result == xs::host::ManagedHostErrorCode::None,
        DescribeManagedHostResult(get_exports_result).c_str());
    XS_CHECK(exports.abi_version == xs::host::XS_MANAGED_ABI_VERSION);
    XS_CHECK(exports.get_abi_version != nullptr);
    XS_CHECK(exports.init != nullptr);
    XS_CHECK(exports.on_message != nullptr);
    XS_CHECK(exports.on_tick != nullptr);
    XS_CHECK(exports.get_abi_version() == xs::host::XS_MANAGED_ABI_VERSION);
    XS_CHECK(exports.init(nullptr) == 0);
    XS_CHECK(exports.on_message(nullptr) == 0);
    XS_CHECK(exports.on_tick(1234, 16) == 0);

    XS_CHECK(host.Unload() == xs::host::ManagedHostErrorCode::None);
    XS_CHECK(!host.IsLoaded());
    XS_CHECK(!host.AreGameExportsBound());
    XS_CHECK(host.load_assembly_and_get_function_pointer() == nullptr);
    XS_CHECK(host.runtime_config_path().empty());
    XS_CHECK(host.assembly_path().empty());
    XS_CHECK(host.hostfxr_path().empty());

    xs::host::ManagedGameExports exports_after_unload{};
    const xs::host::ManagedHostErrorCode after_unload_result = host.GetGameExports(exports_after_unload);
    XS_CHECK(after_unload_result == xs::host::ManagedHostErrorCode::EntryPointNotBound);
}

void TestLoadAndBindServerStubCatalogExportsSucceed()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kManagedRuntimeConfigPath,
            kManagedAssemblyPath,
        });
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());
    XS_CHECK(host.IsLoaded());
    XS_CHECK(!host.AreServerStubCatalogExportsBound());

    xs::host::ManagedServerStubCatalogExports exports_before_bind{};
    const xs::host::ManagedHostErrorCode unbound_result = host.GetServerStubCatalogExports(exports_before_bind);
    XS_CHECK(unbound_result == xs::host::ManagedHostErrorCode::EntryPointNotBound);

    const xs::host::ManagedHostErrorCode bind_result = host.BindServerStubCatalogExports();
    XS_CHECK_MSG(bind_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(bind_result).c_str());
    XS_CHECK(host.AreServerStubCatalogExportsBound());

    const xs::host::ManagedHostErrorCode second_bind_result = host.BindServerStubCatalogExports();
    XS_CHECK(second_bind_result == xs::host::ManagedHostErrorCode::None);

    xs::host::ManagedServerStubCatalogExports exports{};
    const xs::host::ManagedHostErrorCode get_exports_result = host.GetServerStubCatalogExports(exports);
    XS_CHECK_MSG(
        get_exports_result == xs::host::ManagedHostErrorCode::None,
        DescribeManagedHostResult(get_exports_result).c_str());
    XS_CHECK(exports.abi_version == xs::host::XS_MANAGED_ABI_VERSION);
    XS_CHECK(exports.get_count != nullptr);
    XS_CHECK(exports.get_entry != nullptr);

    std::uint32_t count = 0U;
    XS_CHECK(exports.get_count(&count) == 0);
    XS_CHECK(count == 3U);

    struct ExpectedEntry final
    {
        std::string_view entity_type;
        std::string_view entity_id;
    };

    const std::array<ExpectedEntry, 3> expected_entries{
        ExpectedEntry{"ChatService", "unknown"},
        ExpectedEntry{"LeaderboardService", "unknown"},
        ExpectedEntry{"MatchService", "unknown"},
    };

    for (std::uint32_t index = 0U; index < count && index < expected_entries.size(); ++index)
    {
        xs::host::ManagedServerStubCatalogEntry entry{};
        XS_CHECK(exports.get_entry(index, &entry) == 0);
        XS_CHECK(ReadManagedUtf8(entry.entity_type_utf8, entry.entity_type_length) == expected_entries[index].entity_type);
        XS_CHECK(ReadManagedUtf8(entry.entity_id_utf8, entry.entity_id_length) == expected_entries[index].entity_id);
    }
}

void TestLoadAllowsSecondInitializationAfterUnload()
{
    xs::host::ManagedRuntimeHost first_host;

    const xs::host::ManagedHostErrorCode first_load_result = first_host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kManagedRuntimeConfigPath,
            kManagedAssemblyPath,
        });
    XS_CHECK_MSG(first_load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(first_load_result).c_str());

    const xs::host::ManagedHostErrorCode first_bind_result = first_host.BindGameExports();
    XS_CHECK_MSG(first_bind_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(first_bind_result).c_str());

    XS_CHECK(first_host.Unload() == xs::host::ManagedHostErrorCode::None);
    XS_CHECK(!first_host.IsLoaded());
    XS_CHECK(!first_host.AreGameExportsBound());

    xs::host::ManagedRuntimeHost second_host;

    const xs::host::ManagedHostErrorCode second_load_result = second_host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kAbiMismatchRuntimeConfigPath,
            kAbiMismatchAssemblyPath,
        });
    XS_CHECK_MSG(second_load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(second_load_result).c_str());
    XS_CHECK(second_host.IsLoaded());

    const xs::host::ManagedHostErrorCode second_bind_result = second_host.BindGameExports();
    XS_CHECK(second_bind_result == xs::host::ManagedHostErrorCode::AbiVersionMismatch);
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(second_bind_result) ==
        std::string_view("Managed ABI version did not match the native host expectation."));
    XS_CHECK(!second_host.AreGameExportsBound());
}

void TestBindRejectsAbiMismatch()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kAbiMismatchRuntimeConfigPath,
            kAbiMismatchAssemblyPath,
        });
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());

    const xs::host::ManagedHostErrorCode bind_result = host.BindGameExports();
    XS_CHECK(bind_result == xs::host::ManagedHostErrorCode::AbiVersionMismatch);
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(bind_result) ==
        std::string_view("Managed ABI version did not match the native host expectation."));
    XS_CHECK(!host.AreGameExportsBound());

    xs::host::ManagedGameExports exports{};
    const xs::host::ManagedHostErrorCode get_exports_result = host.GetGameExports(exports);
    XS_CHECK(get_exports_result == xs::host::ManagedHostErrorCode::EntryPointNotBound);
}

void TestBindRejectsMissingExport()
{
    xs::host::ManagedRuntimeHost host;

    const xs::host::ManagedHostErrorCode load_result = host.Load(
        xs::host::ManagedRuntimeHostOptions{
            kMissingExportsRuntimeConfigPath,
            kMissingExportsAssemblyPath,
        });
    XS_CHECK_MSG(load_result == xs::host::ManagedHostErrorCode::None, DescribeManagedHostResult(load_result).c_str());

    const xs::host::ManagedHostErrorCode bind_result = host.BindGameExports();
    XS_CHECK(bind_result == xs::host::ManagedHostErrorCode::EntryPointResolveFailed);
    XS_CHECK(
        xs::host::ManagedHostErrorMessage(bind_result) ==
        std::string_view("Failed to resolve a required managed entry point from the GameLogic assembly."));
    XS_CHECK(!host.AreGameExportsBound());
}

} // namespace

int main()
{
    TestManagedAssetsExist();
    TestManagedHostErrorMetadata();
    TestBindRejectsRuntimeNotLoaded();
    TestGetExportsRejectsUnboundHost();
    TestLoadRejectsMissingRuntimeConfig();
    TestLoadRejectsMissingAssembly();
    TestLoadAndBindGameExportsSucceed();
    TestLoadAndBindServerStubCatalogExportsSucceed();
    TestLoadAllowsSecondInitializationAfterUnload();
    TestBindRejectsAbiMismatch();
    TestBindRejectsMissingExport();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " managed host runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

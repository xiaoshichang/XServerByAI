#pragma once

#include "ManagedInterop.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include <coreclr_delegates.h>

namespace xs::host
{

enum class ManagedHostErrorCode : std::int32_t
{
    None = 0,
    AlreadyLoaded = 4000,
    RuntimeConfigPathEmpty = 4001,
    RuntimeConfigPathNotFound = 4002,
    AssemblyPathEmpty = 4003,
    AssemblyPathNotFound = 4004,
    HostfxrPathResolveFailed = 4005,
    HostfxrLibraryLoadFailed = 4006,
    HostfxrExportLoadFailed = 4007,
    RuntimeInitializeFailed = 4008,
    RuntimeDelegateLoadFailed = 4009,
    RuntimeContextCloseFailed = 4010,
    RuntimeNotLoaded = 4011,
    EntryPointResolveFailed = 4012,
    AbiVersionMismatch = 4013,
    EntryPointNotBound = 4014,
};

struct ManagedRuntimeHostOptions
{
    std::filesystem::path runtime_config_path{};
    std::filesystem::path assembly_path{};
};

class ManagedRuntimeHost final
{
  public:
    ManagedRuntimeHost();
    ~ManagedRuntimeHost();

    ManagedRuntimeHost(const ManagedRuntimeHost&) = delete;
    ManagedRuntimeHost& operator=(const ManagedRuntimeHost&) = delete;
    ManagedRuntimeHost(ManagedRuntimeHost&&) = delete;
    ManagedRuntimeHost& operator=(ManagedRuntimeHost&&) = delete;

    [[nodiscard]] ManagedHostErrorCode Load(const ManagedRuntimeHostOptions& options);
    [[nodiscard]] ManagedHostErrorCode Unload() noexcept;
    [[nodiscard]] ManagedHostErrorCode BindGameExports();
    [[nodiscard]] ManagedHostErrorCode GetGameExports(ManagedGameExports& exports) const noexcept;
    [[nodiscard]] ManagedHostErrorCode BindServerStubCatalogExports();
    [[nodiscard]] ManagedHostErrorCode GetServerStubCatalogExports(ManagedServerStubCatalogExports& exports) const noexcept;

    [[nodiscard]] bool IsLoaded() const noexcept;
    [[nodiscard]] bool AreGameExportsBound() const noexcept;
    [[nodiscard]] bool AreServerStubCatalogExportsBound() const noexcept;
    [[nodiscard]] load_assembly_and_get_function_pointer_fn load_assembly_and_get_function_pointer() const noexcept;
    [[nodiscard]] const std::filesystem::path& runtime_config_path() const noexcept;
    [[nodiscard]] const std::filesystem::path& assembly_path() const noexcept;
    [[nodiscard]] const std::filesystem::path& hostfxr_path() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string_view ManagedHostErrorCanonicalName(ManagedHostErrorCode code) noexcept;
[[nodiscard]] std::string_view ManagedHostErrorMessage(ManagedHostErrorCode code) noexcept;

} // namespace xs::host

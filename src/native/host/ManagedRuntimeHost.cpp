#include "ManagedRuntimeHost.h"

#include <hostfxr.h>
#include <nethost.h>

#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace xs::host
{
namespace
{

struct HostfxrExports
{
    hostfxr_initialize_for_runtime_config_fn initialize_for_runtime_config{nullptr};
    hostfxr_get_runtime_delegate_fn get_runtime_delegate{nullptr};
    hostfxr_close_fn close{nullptr};
};

struct ProcessHostfxrState
{
    void* library_handle{nullptr};
    std::filesystem::path library_path{};
    HostfxrExports exports{};
};

std::filesystem::path AbsolutePath(const std::filesystem::path& path)
{
    std::error_code error_code;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error_code);
    if (error_code)
    {
        return path;
    }

    return absolute;
}

bool FileExists(const std::filesystem::path& path) noexcept
{
    std::error_code error_code;
    return std::filesystem::is_regular_file(path, error_code);
}

std::basic_string<char_t> PathToNativeString(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const std::wstring wide = path.native();
    return std::basic_string<char_t>(wide.begin(), wide.end());
#else
    return path.string();
#endif
}

std::basic_string<char_t> Utf8ToNativeString(std::string_view value)
{
    return std::basic_string<char_t>(value.begin(), value.end());
}

std::filesystem::path NativeStringToPath(const std::basic_string<char_t>& value)
{
#if defined(_WIN32)
    return std::filesystem::path(std::wstring(value.begin(), value.end()));
#else
    return std::filesystem::path(value);
#endif
}

void UnloadDynamicLibrary(void* handle) noexcept
{
    if (handle == nullptr)
    {
        return;
    }

#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

void* LoadDynamicLibrary(const std::filesystem::path& path) noexcept
{
#if defined(_WIN32)
    return reinterpret_cast<void*>(LoadLibraryW(path.c_str()));
#else
    dlerror();
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

template <typename T>
T LoadDynamicSymbol(void* library_handle, const char* symbol_name) noexcept
{
#if defined(_WIN32)
    return reinterpret_cast<T>(GetProcAddress(reinterpret_cast<HMODULE>(library_handle), symbol_name));
#else
    dlerror();
    return reinterpret_cast<T>(dlsym(library_handle, symbol_name));
#endif
}

ManagedHostErrorCode LoadHostfxrExports(void* library_handle, HostfxrExports* exports) noexcept
{
    if (library_handle == nullptr || exports == nullptr)
    {
        return ManagedHostErrorCode::HostfxrExportLoadFailed;
    }

    exports->initialize_for_runtime_config =
        LoadDynamicSymbol<hostfxr_initialize_for_runtime_config_fn>(library_handle, "hostfxr_initialize_for_runtime_config");
    exports->get_runtime_delegate =
        LoadDynamicSymbol<hostfxr_get_runtime_delegate_fn>(library_handle, "hostfxr_get_runtime_delegate");
    exports->close = LoadDynamicSymbol<hostfxr_close_fn>(library_handle, "hostfxr_close");

    if (exports->initialize_for_runtime_config == nullptr)
    {
        return ManagedHostErrorCode::HostfxrExportLoadFailed;
    }

    if (exports->get_runtime_delegate == nullptr)
    {
        return ManagedHostErrorCode::HostfxrExportLoadFailed;
    }

    if (exports->close == nullptr)
    {
        return ManagedHostErrorCode::HostfxrExportLoadFailed;
    }

    return ManagedHostErrorCode::None;
}

ManagedHostErrorCode ResolveHostfxrPath(
    const std::filesystem::path& assembly_path,
    std::filesystem::path* hostfxr_path) noexcept
{
    if (hostfxr_path == nullptr)
    {
        return ManagedHostErrorCode::HostfxrPathResolveFailed;
    }

    std::vector<char_t> buffer(4096, static_cast<char_t>(0));
    size_t buffer_size = buffer.size();
    const auto native_assembly_path = PathToNativeString(assembly_path);

    get_hostfxr_parameters parameters{};
    parameters.size = sizeof(parameters);
    parameters.assembly_path = native_assembly_path.c_str();
    parameters.dotnet_root = nullptr;

    int result = get_hostfxr_path(buffer.data(), &buffer_size, &parameters);
    if (result != 0 && buffer_size > buffer.size())
    {
        buffer.assign(buffer_size, static_cast<char_t>(0));
        result = get_hostfxr_path(buffer.data(), &buffer_size, &parameters);
    }

    if (result != 0)
    {
        return ManagedHostErrorCode::HostfxrPathResolveFailed;
    }

    *hostfxr_path = NativeStringToPath(std::basic_string<char_t>(buffer.data()));
    return ManagedHostErrorCode::None;
}

// hostfxr must stay loaded for the lifetime of the process once the CLR is initialized.
ProcessHostfxrState& GetProcessHostfxrState()
{
    static ProcessHostfxrState state{};
    return state;
}

ManagedHostErrorCode EnsureProcessHostfxrLoaded(
    const std::filesystem::path& hostfxr_path,
    ProcessHostfxrState* state) noexcept
{
    if (state == nullptr)
    {
        return ManagedHostErrorCode::HostfxrLibraryLoadFailed;
    }

    if (state->library_handle != nullptr)
    {
        return ManagedHostErrorCode::None;
    }

    state->library_handle = LoadDynamicLibrary(hostfxr_path);
    if (state->library_handle == nullptr)
    {
        return ManagedHostErrorCode::HostfxrLibraryLoadFailed;
    }

    const ManagedHostErrorCode exports_result = LoadHostfxrExports(state->library_handle, &state->exports);
    if (exports_result != ManagedHostErrorCode::None)
    {
        UnloadDynamicLibrary(state->library_handle);
        state->library_handle = nullptr;
        state->library_path.clear();
        state->exports = HostfxrExports{};
        return exports_result;
    }

    state->library_path = hostfxr_path;
    return ManagedHostErrorCode::None;
}

ManagedHostErrorCode ResolveManagedEntryPoint(
    load_assembly_and_get_function_pointer_fn load_function,
    const std::filesystem::path& assembly_path,
    std::string_view type_name,
    std::string_view method_name,
    void** delegate) noexcept
{
    if (delegate == nullptr)
    {
        return ManagedHostErrorCode::EntryPointResolveFailed;
    }

    *delegate = nullptr;

    if (load_function == nullptr)
    {
        return ManagedHostErrorCode::RuntimeNotLoaded;
    }

    const auto native_assembly_path = PathToNativeString(assembly_path);
    const auto native_type_name = Utf8ToNativeString(type_name);
    const auto native_method_name = Utf8ToNativeString(method_name);

    const std::int32_t result = load_function(
        native_assembly_path.c_str(),
        native_type_name.c_str(),
        native_method_name.c_str(),
        UNMANAGEDCALLERSONLY_METHOD,
        nullptr,
        delegate);
    if (result != 0)
    {
        return ManagedHostErrorCode::EntryPointResolveFailed;
    }

    if (*delegate == nullptr)
    {
        return ManagedHostErrorCode::EntryPointResolveFailed;
    }

    return ManagedHostErrorCode::None;
}

} // namespace

class ManagedRuntimeHost::Impl final
{
  public:
    [[nodiscard]] ManagedHostErrorCode Load(const ManagedRuntimeHostOptions& options)
    {
        if (load_assembly_and_get_function_pointer_ != nullptr)
        {
            return ManagedHostErrorCode::AlreadyLoaded;
        }

        if (options.runtime_config_path.empty())
        {
            return ManagedHostErrorCode::RuntimeConfigPathEmpty;
        }

        if (options.assembly_path.empty())
        {
            return ManagedHostErrorCode::AssemblyPathEmpty;
        }

        const std::filesystem::path runtime_config_path = AbsolutePath(options.runtime_config_path);
        const std::filesystem::path assembly_path = AbsolutePath(options.assembly_path);
        if (!FileExists(runtime_config_path))
        {
            return ManagedHostErrorCode::RuntimeConfigPathNotFound;
        }

        if (!FileExists(assembly_path))
        {
            return ManagedHostErrorCode::AssemblyPathNotFound;
        }

        std::filesystem::path hostfxr_path;
        const ManagedHostErrorCode resolve_result = ResolveHostfxrPath(assembly_path, &hostfxr_path);
        if (resolve_result != ManagedHostErrorCode::None)
        {
            return resolve_result;
        }

        ProcessHostfxrState& process_hostfxr_state = GetProcessHostfxrState();
        const ManagedHostErrorCode process_hostfxr_result =
            EnsureProcessHostfxrLoaded(hostfxr_path, &process_hostfxr_state);
        if (process_hostfxr_result != ManagedHostErrorCode::None)
        {
            return process_hostfxr_result;
        }

        hostfxr_handle host_context = nullptr;
        void* raw_delegate = nullptr;
        const auto native_runtime_config_path = PathToNativeString(runtime_config_path);

        const std::int32_t initialize_result = process_hostfxr_state.exports.initialize_for_runtime_config(
            native_runtime_config_path.c_str(),
            nullptr,
            &host_context);
        if (initialize_result < 0)
        {
            return ManagedHostErrorCode::RuntimeInitializeFailed;
        }

        if (host_context == nullptr)
        {
            return ManagedHostErrorCode::RuntimeInitializeFailed;
        }

        const std::int32_t delegate_result = process_hostfxr_state.exports.get_runtime_delegate(
            host_context,
            hdt_load_assembly_and_get_function_pointer,
            &raw_delegate);
        if (delegate_result != 0)
        {
            (void)process_hostfxr_state.exports.close(host_context);
            return ManagedHostErrorCode::RuntimeDelegateLoadFailed;
        }

        const std::int32_t close_result = process_hostfxr_state.exports.close(host_context);
        if (close_result != 0)
        {
            return ManagedHostErrorCode::RuntimeContextCloseFailed;
        }

        if (raw_delegate == nullptr)
        {
            return ManagedHostErrorCode::RuntimeDelegateLoadFailed;
        }

        runtime_config_path_ = runtime_config_path;
        assembly_path_ = assembly_path;
        hostfxr_path_ = process_hostfxr_state.library_path;
        load_assembly_and_get_function_pointer_ = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(raw_delegate);
        game_exports_ = ManagedGameExports{};
        game_exports_bound_ = false;
        return ManagedHostErrorCode::None;
    }

    [[nodiscard]] ManagedHostErrorCode Unload() noexcept
    {
        game_exports_ = ManagedGameExports{};
        game_exports_bound_ = false;
        load_assembly_and_get_function_pointer_ = nullptr;
        runtime_config_path_.clear();
        assembly_path_.clear();
        hostfxr_path_.clear();
        return ManagedHostErrorCode::None;
    }

    [[nodiscard]] ManagedHostErrorCode BindGameExports()
    {
        if (load_assembly_and_get_function_pointer_ == nullptr)
        {
            return ManagedHostErrorCode::RuntimeNotLoaded;
        }

        if (game_exports_bound_)
        {
            return ManagedHostErrorCode::None;
        }

        ManagedGameExports resolved_exports{};

        void* raw_get_abi_version = nullptr;
        ManagedHostErrorCode result = ResolveManagedEntryPoint(
            load_assembly_and_get_function_pointer_,
            assembly_path_,
            kManagedGameExportsTypeName,
            kManagedGameGetAbiVersionMethodName,
            &raw_get_abi_version);
        if (result != ManagedHostErrorCode::None)
        {
            return result;
        }

        resolved_exports.get_abi_version = reinterpret_cast<ManagedGetAbiVersionFn>(raw_get_abi_version);
        resolved_exports.abi_version = resolved_exports.get_abi_version();
        if (resolved_exports.abi_version != XS_MANAGED_ABI_VERSION)
        {
            return ManagedHostErrorCode::AbiVersionMismatch;
        }

        void* raw_init = nullptr;
        result = ResolveManagedEntryPoint(
            load_assembly_and_get_function_pointer_,
            assembly_path_,
            kManagedGameExportsTypeName,
            kManagedGameInitMethodName,
            &raw_init);
        if (result != ManagedHostErrorCode::None)
        {
            return result;
        }
        resolved_exports.init = reinterpret_cast<ManagedInitFn>(raw_init);

        void* raw_on_message = nullptr;
        result = ResolveManagedEntryPoint(
            load_assembly_and_get_function_pointer_,
            assembly_path_,
            kManagedGameExportsTypeName,
            kManagedGameOnMessageMethodName,
            &raw_on_message);
        if (result != ManagedHostErrorCode::None)
        {
            return result;
        }
        resolved_exports.on_message = reinterpret_cast<ManagedOnMessageFn>(raw_on_message);

        void* raw_on_tick = nullptr;
        result = ResolveManagedEntryPoint(
            load_assembly_and_get_function_pointer_,
            assembly_path_,
            kManagedGameExportsTypeName,
            kManagedGameOnTickMethodName,
            &raw_on_tick);
        if (result != ManagedHostErrorCode::None)
        {
            return result;
        }
        resolved_exports.on_tick = reinterpret_cast<ManagedOnTickFn>(raw_on_tick);

        game_exports_ = resolved_exports;
        game_exports_bound_ = true;
        return ManagedHostErrorCode::None;
    }

    [[nodiscard]] ManagedHostErrorCode GetGameExports(ManagedGameExports& exports) const noexcept
    {
        if (!game_exports_bound_)
        {
            return ManagedHostErrorCode::EntryPointNotBound;
        }

        exports = game_exports_;
        return ManagedHostErrorCode::None;
    }

    [[nodiscard]] bool IsLoaded() const noexcept
    {
        return load_assembly_and_get_function_pointer_ != nullptr;
    }

    [[nodiscard]] bool AreGameExportsBound() const noexcept
    {
        return game_exports_bound_;
    }

    [[nodiscard]] load_assembly_and_get_function_pointer_fn load_assembly_and_get_function_pointer() const noexcept
    {
        return load_assembly_and_get_function_pointer_;
    }

    [[nodiscard]] const std::filesystem::path& runtime_config_path() const noexcept
    {
        return runtime_config_path_;
    }

    [[nodiscard]] const std::filesystem::path& assembly_path() const noexcept
    {
        return assembly_path_;
    }

    [[nodiscard]] const std::filesystem::path& hostfxr_path() const noexcept
    {
        return hostfxr_path_;
    }

  private:
    std::filesystem::path runtime_config_path_{};
    std::filesystem::path assembly_path_{};
    std::filesystem::path hostfxr_path_{};
    ManagedGameExports game_exports_{};
    load_assembly_and_get_function_pointer_fn load_assembly_and_get_function_pointer_{nullptr};
    bool game_exports_bound_{false};
};

std::string_view ManagedHostErrorCanonicalName(ManagedHostErrorCode code) noexcept
{
    switch (code)
    {
    case ManagedHostErrorCode::None:
        return "Interop.None";
    case ManagedHostErrorCode::AlreadyLoaded:
        return "Interop.RuntimeAlreadyLoaded";
    case ManagedHostErrorCode::RuntimeConfigPathEmpty:
        return "Interop.RuntimeConfigPathEmpty";
    case ManagedHostErrorCode::RuntimeConfigPathNotFound:
        return "Interop.RuntimeConfigNotFound";
    case ManagedHostErrorCode::AssemblyPathEmpty:
        return "Interop.AssemblyPathEmpty";
    case ManagedHostErrorCode::AssemblyPathNotFound:
        return "Interop.AssemblyNotFound";
    case ManagedHostErrorCode::HostfxrPathResolveFailed:
        return "Interop.HostfxrPathResolveFailed";
    case ManagedHostErrorCode::HostfxrLibraryLoadFailed:
        return "Interop.HostfxrLibraryLoadFailed";
    case ManagedHostErrorCode::HostfxrExportLoadFailed:
        return "Interop.HostfxrExportLoadFailed";
    case ManagedHostErrorCode::RuntimeInitializeFailed:
        return "Interop.RuntimeInitializeFailed";
    case ManagedHostErrorCode::RuntimeDelegateLoadFailed:
        return "Interop.RuntimeDelegateLoadFailed";
    case ManagedHostErrorCode::RuntimeContextCloseFailed:
        return "Interop.RuntimeContextCloseFailed";
    case ManagedHostErrorCode::RuntimeNotLoaded:
        return "Interop.RuntimeNotLoaded";
    case ManagedHostErrorCode::EntryPointResolveFailed:
        return "Interop.EntryPointResolveFailed";
    case ManagedHostErrorCode::AbiVersionMismatch:
        return "Interop.AbiVersionMismatch";
    case ManagedHostErrorCode::EntryPointNotBound:
        return "Interop.EntryPointNotBound";
    }

    return "Interop.Unknown";
}

std::string_view ManagedHostErrorMessage(ManagedHostErrorCode code) noexcept
{
    switch (code)
    {
    case ManagedHostErrorCode::None:
        return "No error.";
    case ManagedHostErrorCode::AlreadyLoaded:
        return "Managed runtime host is already loaded.";
    case ManagedHostErrorCode::RuntimeConfigPathEmpty:
        return "Managed runtime config path must not be empty.";
    case ManagedHostErrorCode::RuntimeConfigPathNotFound:
        return "Managed runtime config was not found.";
    case ManagedHostErrorCode::AssemblyPathEmpty:
        return "Managed root assembly path must not be empty.";
    case ManagedHostErrorCode::AssemblyPathNotFound:
        return "Managed root assembly was not found.";
    case ManagedHostErrorCode::HostfxrPathResolveFailed:
        return "Failed to resolve the hostfxr library path.";
    case ManagedHostErrorCode::HostfxrLibraryLoadFailed:
        return "Failed to load the hostfxr library.";
    case ManagedHostErrorCode::HostfxrExportLoadFailed:
        return "Failed to resolve required hostfxr exports.";
    case ManagedHostErrorCode::RuntimeInitializeFailed:
        return "Failed to initialize the CLR runtime from runtimeconfig.json.";
    case ManagedHostErrorCode::RuntimeDelegateLoadFailed:
        return "Failed to resolve load_assembly_and_get_function_pointer from hostfxr.";
    case ManagedHostErrorCode::RuntimeContextCloseFailed:
        return "Failed to close the temporary hostfxr context.";
    case ManagedHostErrorCode::RuntimeNotLoaded:
        return "Managed runtime host must be loaded before binding game exports.";
    case ManagedHostErrorCode::EntryPointResolveFailed:
        return "Failed to resolve a required managed entry point from the GameLogic assembly.";
    case ManagedHostErrorCode::AbiVersionMismatch:
        return "Managed ABI version did not match the native host expectation.";
    case ManagedHostErrorCode::EntryPointNotBound:
        return "Managed game entry points have not been bound successfully.";
    }

    return "Unknown managed host error.";
}

ManagedRuntimeHost::ManagedRuntimeHost()
    : impl_(std::make_unique<Impl>())
{
}

ManagedRuntimeHost::~ManagedRuntimeHost()
{
    if (impl_ != nullptr)
    {
        (void)impl_->Unload();
    }
}

ManagedHostErrorCode ManagedRuntimeHost::Load(const ManagedRuntimeHostOptions& options)
{
    return impl_->Load(options);
}

ManagedHostErrorCode ManagedRuntimeHost::Unload() noexcept
{
    return impl_->Unload();
}

ManagedHostErrorCode ManagedRuntimeHost::BindGameExports()
{
    return impl_->BindGameExports();
}

ManagedHostErrorCode ManagedRuntimeHost::GetGameExports(ManagedGameExports& exports) const noexcept
{
    return impl_->GetGameExports(exports);
}

bool ManagedRuntimeHost::IsLoaded() const noexcept
{
    return impl_->IsLoaded();
}

bool ManagedRuntimeHost::AreGameExportsBound() const noexcept
{
    return impl_->AreGameExportsBound();
}

load_assembly_and_get_function_pointer_fn ManagedRuntimeHost::load_assembly_and_get_function_pointer() const noexcept
{
    return impl_->load_assembly_and_get_function_pointer();
}

const std::filesystem::path& ManagedRuntimeHost::runtime_config_path() const noexcept
{
    return impl_->runtime_config_path();
}

const std::filesystem::path& ManagedRuntimeHost::assembly_path() const noexcept
{
    return impl_->assembly_path();
}

const std::filesystem::path& ManagedRuntimeHost::hostfxr_path() const noexcept
{
    return impl_->hostfxr_path();
}

} // namespace xs::host

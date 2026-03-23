#include "ManagedRuntimeHost.h"

#include <hostfxr.h>
#include <nethost.h>

#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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
    hostfxr_set_error_writer_fn set_error_writer{nullptr};
};

void ClearError(std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

ManagedHostErrorCode SetError(ManagedHostErrorCode code, std::string message, std::string* error_message)
{
    if (error_message != nullptr)
    {
        *error_message = std::move(message);
    }

    return code;
}

std::string PathToUtf8(const std::filesystem::path& path)
{
#if defined(_WIN32)
    const auto raw = path.u8string();
    return std::string(raw.begin(), raw.end());
#else
    return path.string();
#endif
}

std::string FormatResultCode(std::int32_t result)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(result);
    return stream.str();
}

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

std::filesystem::path NativeStringToPath(const std::basic_string<char_t>& value)
{
#if defined(_WIN32)
    return std::filesystem::path(std::wstring(value.begin(), value.end()));
#else
    return std::filesystem::path(value);
#endif
}

std::string NativeStringToUtf8(std::basic_string_view<char_t> value)
{
#if defined(_WIN32)
    if (value.empty())
    {
        return {};
    }

    const std::wstring wide(value.begin(), value.end());
    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required_size <= 0)
    {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required_size), '\0');
    const int converted_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        utf8.data(),
        required_size,
        nullptr,
        nullptr);
    if (converted_size != required_size)
    {
        return {};
    }

    return utf8;
#else
    return std::string(value.begin(), value.end());
#endif
}

std::string NativeStringToUtf8(const char_t* value)
{
    if (value == nullptr || *value == static_cast<char_t>(0))
    {
        return {};
    }

    std::basic_string<char_t> text;
    const char_t* current = value;
    while (*current != static_cast<char_t>(0))
    {
        text.push_back(*current);
        ++current;
    }

    return NativeStringToUtf8(text);
}

std::string BuildHostfxrFailureMessage(std::string_view operation, std::int32_t result, std::string_view details = {})
{
    std::string message(operation);
    message += " failed with code ";
    message += FormatResultCode(result);
    if (!details.empty())
    {
        message += ": ";
        message += details;
    }

    return message;
}

#if defined(_WIN32)
std::string LastSystemErrorMessage()
{
    const DWORD error_code = GetLastError();
    if (error_code == 0)
    {
        return {};
    }

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);
    if (length == 0 || buffer == nullptr)
    {
        return "Windows error " + std::to_string(error_code);
    }

    std::wstring wide(buffer, buffer + length);
    LocalFree(buffer);

    while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n'))
    {
        wide.pop_back();
    }

    if (wide.empty())
    {
        return {};
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (required_size <= 0)
    {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required_size), '\0');
    const int converted_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wide.c_str(),
        static_cast<int>(wide.size()),
        utf8.data(),
        required_size,
        nullptr,
        nullptr);
    if (converted_size != required_size)
    {
        return {};
    }

    return utf8;
}
#else
std::string LastSystemErrorMessage()
{
    const char* error = dlerror();
    if (error == nullptr)
    {
        return {};
    }

    return std::string(error);
}
#endif

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

void* LoadDynamicLibrary(const std::filesystem::path& path, std::string* error_message)
{
#if defined(_WIN32)
    HMODULE library = LoadLibraryW(path.c_str());
    if (library == nullptr)
    {
        std::string message = "Failed to load hostfxr library: ";
        message += PathToUtf8(path);
        const std::string detail = LastSystemErrorMessage();
        if (!detail.empty())
        {
            message += ": ";
            message += detail;
        }

        if (error_message != nullptr)
        {
            *error_message = std::move(message);
        }

        return nullptr;
    }

    ClearError(error_message);
    return reinterpret_cast<void*>(library);
#else
    dlerror();
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr)
    {
        std::string message = "Failed to load hostfxr library: ";
        message += PathToUtf8(path);
        const std::string detail = LastSystemErrorMessage();
        if (!detail.empty())
        {
            message += ": ";
            message += detail;
        }

        if (error_message != nullptr)
        {
            *error_message = std::move(message);
        }

        return nullptr;
    }

    ClearError(error_message);
    return library;
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

ManagedHostErrorCode LoadHostfxrExports(
    void* library_handle,
    const std::filesystem::path& library_path,
    HostfxrExports* exports,
    std::string* error_message)
{
    if (library_handle == nullptr || exports == nullptr)
    {
        return SetError(
            ManagedHostErrorCode::HostfxrExportLoadFailed,
            "Hostfxr export loading requires non-null inputs.",
            error_message);
    }

    exports->initialize_for_runtime_config =
        LoadDynamicSymbol<hostfxr_initialize_for_runtime_config_fn>(library_handle, "hostfxr_initialize_for_runtime_config");
    exports->get_runtime_delegate =
        LoadDynamicSymbol<hostfxr_get_runtime_delegate_fn>(library_handle, "hostfxr_get_runtime_delegate");
    exports->close = LoadDynamicSymbol<hostfxr_close_fn>(library_handle, "hostfxr_close");
    exports->set_error_writer = LoadDynamicSymbol<hostfxr_set_error_writer_fn>(library_handle, "hostfxr_set_error_writer");

    if (exports->initialize_for_runtime_config == nullptr)
    {
        return SetError(
            ManagedHostErrorCode::HostfxrExportLoadFailed,
            "Failed to load hostfxr export 'hostfxr_initialize_for_runtime_config' from " + PathToUtf8(library_path) + ".",
            error_message);
    }

    if (exports->get_runtime_delegate == nullptr)
    {
        return SetError(
            ManagedHostErrorCode::HostfxrExportLoadFailed,
            "Failed to load hostfxr export 'hostfxr_get_runtime_delegate' from " + PathToUtf8(library_path) + ".",
            error_message);
    }

    if (exports->close == nullptr)
    {
        return SetError(
            ManagedHostErrorCode::HostfxrExportLoadFailed,
            "Failed to load hostfxr export 'hostfxr_close' from " + PathToUtf8(library_path) + ".",
            error_message);
    }

    ClearError(error_message);
    return ManagedHostErrorCode::None;
}

ManagedHostErrorCode ResolveHostfxrPath(
    const std::filesystem::path& assembly_path,
    std::filesystem::path* hostfxr_path,
    std::string* error_message)
{
    if (hostfxr_path == nullptr)
    {
        return SetError(
            ManagedHostErrorCode::HostfxrPathResolveFailed,
            "Hostfxr path output must not be null.",
            error_message);
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
        return SetError(
            ManagedHostErrorCode::HostfxrPathResolveFailed,
            BuildHostfxrFailureMessage("get_hostfxr_path", result),
            error_message);
    }

    *hostfxr_path = NativeStringToPath(std::basic_string<char_t>(buffer.data()));
    ClearError(error_message);
    return ManagedHostErrorCode::None;
}

thread_local std::basic_string<char_t>* g_hostfxr_error_sink = nullptr;

void HOSTFXR_CALLTYPE CollectHostfxrError(const char_t* message)
{
    if (g_hostfxr_error_sink == nullptr || message == nullptr)
    {
        return;
    }

    if (!g_hostfxr_error_sink->empty())
    {
        g_hostfxr_error_sink->push_back(static_cast<char_t>('\n'));
    }

    g_hostfxr_error_sink->append(message);
}

class ScopedHostfxrErrorWriter final
{
  public:
    explicit ScopedHostfxrErrorWriter(hostfxr_set_error_writer_fn set_error_writer)
        : set_error_writer_(set_error_writer)
    {
        if (set_error_writer_ == nullptr)
        {
            return;
        }

        g_hostfxr_error_sink = &messages_;
        previous_writer_ = set_error_writer_(CollectHostfxrError);
    }

    ~ScopedHostfxrErrorWriter()
    {
        if (set_error_writer_ != nullptr)
        {
            set_error_writer_(previous_writer_);
        }

        g_hostfxr_error_sink = nullptr;
    }

    [[nodiscard]] std::string message() const
    {
        return NativeStringToUtf8(messages_);
    }

  private:
    hostfxr_set_error_writer_fn set_error_writer_{nullptr};
    hostfxr_error_writer_fn previous_writer_{nullptr};
    std::basic_string<char_t> messages_{};
};

} // namespace

class ManagedRuntimeHost::Impl final
{
  public:
    [[nodiscard]] ManagedHostErrorCode Load(const ManagedRuntimeHostOptions& options, std::string* error_message)
    {
        if (load_assembly_and_get_function_pointer_ != nullptr)
        {
            return SetError(
                ManagedHostErrorCode::AlreadyLoaded,
                "Managed runtime host is already loaded.",
                error_message);
        }

        if (options.runtime_config_path.empty())
        {
            return SetError(
                ManagedHostErrorCode::RuntimeConfigPathEmpty,
                "Managed runtime config path must not be empty.",
                error_message);
        }

        if (options.assembly_path.empty())
        {
            return SetError(
                ManagedHostErrorCode::AssemblyPathEmpty,
                "Managed root assembly path must not be empty.",
                error_message);
        }

        const std::filesystem::path runtime_config_path = AbsolutePath(options.runtime_config_path);
        const std::filesystem::path assembly_path = AbsolutePath(options.assembly_path);
        if (!FileExists(runtime_config_path))
        {
            return SetError(
                ManagedHostErrorCode::RuntimeConfigPathNotFound,
                "Managed runtime config was not found: " + PathToUtf8(runtime_config_path),
                error_message);
        }

        if (!FileExists(assembly_path))
        {
            return SetError(
                ManagedHostErrorCode::AssemblyPathNotFound,
                "Managed root assembly was not found: " + PathToUtf8(assembly_path),
                error_message);
        }

        std::filesystem::path hostfxr_path;
        std::string resolve_error;
        const ManagedHostErrorCode resolve_result = ResolveHostfxrPath(assembly_path, &hostfxr_path, &resolve_error);
        if (resolve_result != ManagedHostErrorCode::None)
        {
            return SetError(
                resolve_result,
                "Failed to resolve hostfxr path for '" + PathToUtf8(assembly_path) + "': " + resolve_error,
                error_message);
        }

        std::string load_library_error;
        void* library_handle = LoadDynamicLibrary(hostfxr_path, &load_library_error);
        if (library_handle == nullptr)
        {
            return SetError(
                ManagedHostErrorCode::HostfxrLibraryLoadFailed,
                std::move(load_library_error),
                error_message);
        }

        HostfxrExports exports{};
        std::string exports_error;
        const ManagedHostErrorCode exports_result =
            LoadHostfxrExports(library_handle, hostfxr_path, &exports, &exports_error);
        if (exports_result != ManagedHostErrorCode::None)
        {
            UnloadDynamicLibrary(library_handle);
            return SetError(exports_result, std::move(exports_error), error_message);
        }

        hostfxr_handle host_context = nullptr;
        void* raw_delegate = nullptr;

        const auto native_runtime_config_path = PathToNativeString(runtime_config_path);
        {
            ScopedHostfxrErrorWriter writer(exports.set_error_writer);
            const std::int32_t initialize_result =
                exports.initialize_for_runtime_config(native_runtime_config_path.c_str(), nullptr, &host_context);
            const std::string detail = writer.message();
            if (initialize_result < 0)
            {
                UnloadDynamicLibrary(library_handle);
                return SetError(
                    ManagedHostErrorCode::RuntimeInitializeFailed,
                    BuildHostfxrFailureMessage("hostfxr_initialize_for_runtime_config", initialize_result, detail),
                    error_message);
            }
        }

        if (host_context == nullptr)
        {
            UnloadDynamicLibrary(library_handle);
            return SetError(
                ManagedHostErrorCode::RuntimeInitializeFailed,
                "hostfxr_initialize_for_runtime_config returned success without a host context.",
                error_message);
        }

        {
            ScopedHostfxrErrorWriter writer(exports.set_error_writer);
            const std::int32_t delegate_result = exports.get_runtime_delegate(
                host_context,
                hdt_load_assembly_and_get_function_pointer,
                &raw_delegate);
            const std::string detail = writer.message();
            if (delegate_result != 0)
            {
                (void)exports.close(host_context);
                UnloadDynamicLibrary(library_handle);
                return SetError(
                    ManagedHostErrorCode::RuntimeDelegateLoadFailed,
                    BuildHostfxrFailureMessage("hostfxr_get_runtime_delegate", delegate_result, detail),
                    error_message);
            }
        }

        const std::int32_t close_result = exports.close(host_context);
        if (close_result != 0)
        {
            UnloadDynamicLibrary(library_handle);
            return SetError(
                ManagedHostErrorCode::RuntimeContextCloseFailed,
                BuildHostfxrFailureMessage("hostfxr_close", close_result),
                error_message);
        }

        if (raw_delegate == nullptr)
        {
            UnloadDynamicLibrary(library_handle);
            return SetError(
                ManagedHostErrorCode::RuntimeDelegateLoadFailed,
                "hostfxr returned a null load_assembly_and_get_function_pointer delegate.",
                error_message);
        }

        runtime_config_path_ = runtime_config_path;
        assembly_path_ = assembly_path;
        hostfxr_path_ = hostfxr_path;
        hostfxr_library_ = library_handle;
        exports_ = exports;
        load_assembly_and_get_function_pointer_ = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(raw_delegate);

        ClearError(error_message);
        return ManagedHostErrorCode::None;
    }

    [[nodiscard]] ManagedHostErrorCode Unload(std::string* error_message) noexcept
    {
        load_assembly_and_get_function_pointer_ = nullptr;
        exports_ = HostfxrExports{};
        runtime_config_path_.clear();
        assembly_path_.clear();
        hostfxr_path_.clear();

        if (hostfxr_library_ != nullptr)
        {
            UnloadDynamicLibrary(hostfxr_library_);
            hostfxr_library_ = nullptr;
        }

        ClearError(error_message);
        return ManagedHostErrorCode::None;
    }

    [[nodiscard]] bool IsLoaded() const noexcept
    {
        return load_assembly_and_get_function_pointer_ != nullptr;
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
    void* hostfxr_library_{nullptr};
    HostfxrExports exports_{};
    load_assembly_and_get_function_pointer_fn load_assembly_and_get_function_pointer_{nullptr};
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
        (void)impl_->Unload(nullptr);
    }
}

ManagedHostErrorCode ManagedRuntimeHost::Load(const ManagedRuntimeHostOptions& options, std::string* error_message)
{
    return impl_->Load(options, error_message);
}

ManagedHostErrorCode ManagedRuntimeHost::Unload(std::string* error_message) noexcept
{
    return impl_->Unload(error_message);
}

bool ManagedRuntimeHost::IsLoaded() const noexcept
{
    return impl_->IsLoaded();
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



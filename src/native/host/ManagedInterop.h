#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#if defined(_WIN32)
#define XS_MANAGED_CALLTYPE __cdecl
#else
#define XS_MANAGED_CALLTYPE
#endif

namespace xs::host
{

inline constexpr std::uint32_t XS_MANAGED_ABI_VERSION = 1;
inline constexpr std::string_view kManagedGameExportsTypeName =
    "XServer.Managed.GameLogic.Interop.GameNativeExports, XServer.Managed.GameLogic";
inline constexpr std::string_view kManagedGameGetAbiVersionMethodName = "GameNativeGetAbiVersion";
inline constexpr std::string_view kManagedGameInitMethodName = "GameNativeInit";
inline constexpr std::string_view kManagedGameOnMessageMethodName = "GameNativeOnMessage";
inline constexpr std::string_view kManagedGameOnTickMethodName = "GameNativeOnTick";
inline constexpr std::string_view kManagedGameGetServerStubCatalogCountMethodName =
    "GameNativeGetServerStubCatalogCount";
inline constexpr std::string_view kManagedGameGetServerStubCatalogEntryMethodName =
    "GameNativeGetServerStubCatalogEntry";
inline constexpr std::size_t XS_MANAGED_SERVER_STUB_ENTITY_TYPE_MAX_UTF8_BYTES = 128u;
inline constexpr std::size_t XS_MANAGED_SERVER_STUB_ENTITY_ID_MAX_UTF8_BYTES = 128u;

struct ManagedInitArgs
{
    std::uint32_t struct_size;
    std::uint32_t abi_version;
    std::uint16_t process_type;
    std::uint16_t reserved0;
    const std::uint8_t* node_id_utf8;
    std::uint32_t node_id_length;
    const std::uint8_t* config_path_utf8;
    std::uint32_t config_path_length;
};

struct ManagedMessageView
{
    std::uint32_t struct_size;
    std::uint32_t msg_id;
    std::uint32_t seq;
    std::uint32_t flags;
    std::uint64_t session_id;
    std::uint64_t player_id;
    const std::uint8_t* payload;
    std::uint32_t payload_length;
    std::uint32_t reserved0;
};

struct ManagedServerStubCatalogEntry
{
    std::uint32_t struct_size{sizeof(ManagedServerStubCatalogEntry)};
    std::uint32_t entity_type_length{0u};
    std::uint8_t entity_type_utf8[XS_MANAGED_SERVER_STUB_ENTITY_TYPE_MAX_UTF8_BYTES]{};
    std::uint32_t entity_id_length{0u};
    std::uint8_t entity_id_utf8[XS_MANAGED_SERVER_STUB_ENTITY_ID_MAX_UTF8_BYTES]{};
    std::uint32_t reserved0{0u};
};

using ManagedGetAbiVersionFn = std::uint32_t (XS_MANAGED_CALLTYPE*)();
using ManagedInitFn = std::int32_t (XS_MANAGED_CALLTYPE*)(const ManagedInitArgs* args);
using ManagedOnMessageFn = std::int32_t (XS_MANAGED_CALLTYPE*)(const ManagedMessageView* message);
using ManagedOnTickFn = std::int32_t (XS_MANAGED_CALLTYPE*)(std::uint64_t now_unix_ms_utc, std::uint32_t delta_ms);
using ManagedGetServerStubCatalogCountFn = std::int32_t (XS_MANAGED_CALLTYPE*)(std::uint32_t* count);
using ManagedGetServerStubCatalogEntryFn =
    std::int32_t (XS_MANAGED_CALLTYPE*)(std::uint32_t index, ManagedServerStubCatalogEntry* entry);

struct ManagedGameExports
{
    std::uint32_t abi_version{0};
    ManagedGetAbiVersionFn get_abi_version{nullptr};
    ManagedInitFn init{nullptr};
    ManagedOnMessageFn on_message{nullptr};
    ManagedOnTickFn on_tick{nullptr};
};

struct ManagedServerStubCatalogExports
{
    std::uint32_t abi_version{0};
    ManagedGetServerStubCatalogCountFn get_count{nullptr};
    ManagedGetServerStubCatalogEntryFn get_entry{nullptr};
};

} // namespace xs::host

#undef XS_MANAGED_CALLTYPE

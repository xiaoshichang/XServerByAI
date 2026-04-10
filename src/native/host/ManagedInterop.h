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

inline constexpr std::uint32_t XS_MANAGED_ABI_VERSION = 8;
inline constexpr std::string_view kManagedExportsTypeName =
    "XServer.Managed.Framework.Interop.GameNativeExports, XServer.Managed.Framework";
inline constexpr std::string_view kManagedGameGetAbiVersionMethodName = "GameNativeGetAbiVersion";
inline constexpr std::string_view kManagedGameInitMethodName = "GameNativeInit";
inline constexpr std::string_view kManagedGameOnMessageMethodName = "GameNativeOnMessage";
inline constexpr std::string_view kManagedGameOnTickMethodName = "GameNativeOnTick";
inline constexpr std::string_view kManagedGameOnNativeTimerMethodName = "GameNativeOnNativeTimer";
inline constexpr std::string_view kManagedGameApplyServerStubOwnershipMethodName = "GameNativeApplyServerStubOwnership";
inline constexpr std::string_view kManagedGameResetServerStubOwnershipMethodName = "GameNativeResetServerStubOwnership";
inline constexpr std::string_view kManagedGameGetReadyServerStubCountMethodName = "GameNativeGetReadyServerStubCount";
inline constexpr std::string_view kManagedGameGetReadyServerStubEntryMethodName = "GameNativeGetReadyServerStubEntry";
inline constexpr std::string_view kManagedGameGetServerStubCatalogCountMethodName =
    "GameNativeGetServerStubCatalogCount";
inline constexpr std::string_view kManagedGameGetServerStubCatalogEntryMethodName =
    "GameNativeGetServerStubCatalogEntry";
inline constexpr std::size_t XS_MANAGED_NODE_ID_MAX_UTF8_BYTES = 128u;
inline constexpr std::size_t XS_MANAGED_SERVER_STUB_ENTITY_TYPE_MAX_UTF8_BYTES = 128u;
inline constexpr std::size_t XS_MANAGED_SERVER_STUB_ENTITY_ID_MAX_UTF8_BYTES = 128u;

struct ManagedServerStubReadyEntry;

enum class ManagedLogLevel : std::uint32_t
{
    Trace = 0u,
    Debug = 1u,
    Info = 2u,
    Warn = 3u,
    Error = 4u,
    Fatal = 5u,
};

using ManagedOnServerStubReadyCallbackFn = void(XS_MANAGED_CALLTYPE*)(void* context, std::uint64_t assignment_epoch,
                                                                      const ManagedServerStubReadyEntry* entry);
using ManagedOnLogCallbackFn = void(XS_MANAGED_CALLTYPE*)(void* context, std::uint32_t level,
                                                          const std::uint8_t* category_utf8,
                                                          std::uint32_t category_length,
                                                          const std::uint8_t* message_utf8,
                                                          std::uint32_t message_length);
using ManagedCreateOnceTimerCallbackFn = std::int64_t(XS_MANAGED_CALLTYPE*)(void* context, std::uint64_t delay_ms);
using ManagedCancelTimerCallbackFn = std::int32_t(XS_MANAGED_CALLTYPE*)(void* context, std::int64_t timer_id);
using ManagedForwardStubCallCallbackFn =
    std::int32_t(XS_MANAGED_CALLTYPE*)(void* context,
                                       const std::uint8_t* target_game_node_id_utf8,
                                       std::uint32_t target_game_node_id_length,
                                       const std::uint8_t* target_stub_type_utf8,
                                       std::uint32_t target_stub_type_length,
                                       std::uint32_t msg_id,
                                       const std::uint8_t* payload,
                                       std::uint32_t payload_length);
using ManagedForwardProxyCallCallbackFn =
    std::int32_t(XS_MANAGED_CALLTYPE*)(void* context,
                                       const std::uint8_t* route_gate_node_id_utf8,
                                       std::uint32_t route_gate_node_id_length,
                                       const std::uint8_t* target_entity_id_utf8,
                                       std::uint32_t target_entity_id_length,
                                       std::uint32_t msg_id,
                                       const std::uint8_t* payload,
                                       std::uint32_t payload_length);

struct ManagedNativeCallbacks
{
    std::uint32_t struct_size{sizeof(ManagedNativeCallbacks)};
    std::uint32_t reserved0{0u};
    void* context{nullptr};
    ManagedOnServerStubReadyCallbackFn on_server_stub_ready{nullptr};
    ManagedOnLogCallbackFn on_log{nullptr};
    ManagedCreateOnceTimerCallbackFn create_once_timer{nullptr};
    ManagedCancelTimerCallbackFn cancel_timer{nullptr};
    ManagedForwardStubCallCallbackFn forward_stub_call{nullptr};
    ManagedForwardProxyCallCallbackFn forward_proxy_call{nullptr};
};

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
    ManagedNativeCallbacks native_callbacks{};
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

struct ManagedServerStubOwnershipEntry
{
    std::uint32_t struct_size{sizeof(ManagedServerStubOwnershipEntry)};
    std::uint32_t entity_type_length{0u};
    std::uint8_t entity_type_utf8[XS_MANAGED_SERVER_STUB_ENTITY_TYPE_MAX_UTF8_BYTES]{};
    std::uint32_t entity_id_length{0u};
    std::uint8_t entity_id_utf8[XS_MANAGED_SERVER_STUB_ENTITY_ID_MAX_UTF8_BYTES]{};
    std::uint32_t owner_game_node_id_length{0u};
    std::uint8_t owner_game_node_id_utf8[XS_MANAGED_NODE_ID_MAX_UTF8_BYTES]{};
    std::uint32_t entry_flags{0u};
};

struct ManagedServerStubOwnershipSync
{
    std::uint32_t struct_size{sizeof(ManagedServerStubOwnershipSync)};
    std::uint32_t status_flags{0u};
    std::uint64_t assignment_epoch{0u};
    std::uint64_t server_now_unix_ms{0u};
    std::uint32_t assignment_count{0u};
    std::uint32_t reserved0{0u};
    ManagedServerStubOwnershipEntry* assignments{nullptr};
};

struct ManagedServerStubReadyEntry
{
    std::uint32_t struct_size{sizeof(ManagedServerStubReadyEntry)};
    std::uint32_t entity_type_length{0u};
    std::uint8_t entity_type_utf8[XS_MANAGED_SERVER_STUB_ENTITY_TYPE_MAX_UTF8_BYTES]{};
    std::uint32_t entity_id_length{0u};
    std::uint8_t entity_id_utf8[XS_MANAGED_SERVER_STUB_ENTITY_ID_MAX_UTF8_BYTES]{};
    std::uint8_t ready{0u};
    std::uint8_t reserved0[3]{};
    std::uint32_t entry_flags{0u};
};

using ManagedGetAbiVersionFn = std::uint32_t(XS_MANAGED_CALLTYPE*)();
using ManagedInitFn = std::int32_t(XS_MANAGED_CALLTYPE*)(const ManagedInitArgs* args);
using ManagedOnMessageFn = std::int32_t(XS_MANAGED_CALLTYPE*)(const ManagedMessageView* message);
using ManagedOnTickFn = std::int32_t(XS_MANAGED_CALLTYPE*)(std::uint64_t now_unix_ms_utc, std::uint32_t delta_ms);
using ManagedOnNativeTimerFn = std::int32_t(XS_MANAGED_CALLTYPE*)(std::int64_t timer_id);
using ManagedApplyServerStubOwnershipFn =
    std::int32_t(XS_MANAGED_CALLTYPE*)(const ManagedServerStubOwnershipSync* sync);
using ManagedResetServerStubOwnershipFn = std::int32_t(XS_MANAGED_CALLTYPE*)();
using ManagedGetReadyServerStubCountFn = std::int32_t(XS_MANAGED_CALLTYPE*)(std::uint32_t* count);
using ManagedGetReadyServerStubEntryFn = std::int32_t(XS_MANAGED_CALLTYPE*)(std::uint32_t index,
                                                                            ManagedServerStubReadyEntry* entry);
using ManagedGetServerStubCatalogCountFn = std::int32_t(XS_MANAGED_CALLTYPE*)(std::uint32_t* count);
using ManagedGetServerStubCatalogEntryFn = std::int32_t(XS_MANAGED_CALLTYPE*)(std::uint32_t index,
                                                                              ManagedServerStubCatalogEntry* entry);

struct ManagedExports
{
    std::uint32_t abi_version{0};
    ManagedGetAbiVersionFn get_abi_version{nullptr};
    ManagedInitFn init{nullptr};
    ManagedOnMessageFn on_message{nullptr};
    ManagedOnTickFn on_tick{nullptr};
    ManagedOnNativeTimerFn on_native_timer{nullptr};
    ManagedApplyServerStubOwnershipFn apply_server_stub_ownership{nullptr};
    ManagedResetServerStubOwnershipFn reset_server_stub_ownership{nullptr};
    ManagedGetReadyServerStubCountFn get_ready_server_stub_count{nullptr};
    ManagedGetReadyServerStubEntryFn get_ready_server_stub_entry{nullptr};
    ManagedGetServerStubCatalogCountFn get_server_stub_catalog_count{nullptr};
    ManagedGetServerStubCatalogEntryFn get_server_stub_catalog_entry{nullptr};
};

} // namespace xs::host

#undef XS_MANAGED_CALLTYPE

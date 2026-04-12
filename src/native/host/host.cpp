#include "ManagedRuntimeHost.h"

#include <cstddef>
#include <type_traits>
#include <utility>

static_assert(std::is_default_constructible_v<xs::host::ManagedRuntimeHost>,
              "ManagedRuntimeHost must remain default constructible.");
static_assert(std::is_same_v<decltype(std::declval<const xs::host::ManagedRuntimeHost&>().IsLoaded()), bool>,
              "ManagedRuntimeHost::IsLoaded must return bool.");
static_assert(std::is_same_v<decltype(std::declval<const xs::host::ManagedRuntimeHost&>().AreExportsBound()), bool>,
              "ManagedRuntimeHost::AreExportsBound must return bool.");
static_assert(std::is_same_v<decltype(std::declval<const xs::host::ManagedRuntimeHost&>()
                                          .load_assembly_and_get_function_pointer()),
                             load_assembly_and_get_function_pointer_fn>,
              "ManagedRuntimeHost must expose load_assembly_and_get_function_pointer delegate type.");
static_assert(std::is_same_v<decltype(&xs::host::ManagedRuntimeHost::Load),
                             xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)(
                                 const xs::host::ManagedRuntimeHostOptions&)>,
              "ManagedRuntimeHost::Load must return only ManagedHostErrorCode.");
static_assert(std::is_same_v<decltype(&xs::host::ManagedRuntimeHost::Unload),
                             xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)() noexcept>,
              "ManagedRuntimeHost::Unload must return only ManagedHostErrorCode.");
static_assert(std::is_same_v<decltype(&xs::host::ManagedRuntimeHost::BindExports),
                             xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)()>,
              "ManagedRuntimeHost::BindExports must return only ManagedHostErrorCode.");
static_assert(std::is_same_v<decltype(&xs::host::ManagedRuntimeHost::GetExports),
                             xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)(
                                 xs::host::ManagedExports&) const noexcept>,
              "ManagedRuntimeHost::GetExports must return only ManagedHostErrorCode plus output exports.");
static_assert(xs::host::XS_MANAGED_ABI_VERSION == 9u,
              "Managed ABI version must remain aligned with current managed interop docs.");
static_assert(offsetof(xs::host::ManagedInitArgs, abi_version) == 4,
              "ManagedInitArgs::abi_version offset must remain ABI compatible.");
static_assert(sizeof(xs::host::ManagedLogLevel) == 4u,
              "ManagedLogLevel must remain ABI compatible with managed interop.");
static_assert(sizeof(xs::host::ManagedNativeCallbacks) == 72u,
              "ManagedNativeCallbacks must remain ABI compatible with managed interop.");
static_assert(sizeof(xs::host::ManagedInitArgs) == 120u,
              "ManagedInitArgs must remain ABI compatible with managed interop.");
static_assert(offsetof(xs::host::ManagedInitArgs, native_callbacks) >
                  offsetof(xs::host::ManagedInitArgs, config_path_length),
              "ManagedInitArgs native callbacks order must remain ABI compatible.");
static_assert(offsetof(xs::host::ManagedMessageView, payload) > offsetof(xs::host::ManagedMessageView, player_id),
              "ManagedMessageView payload field order must remain ABI compatible.");
static_assert(sizeof(xs::host::ManagedServerStubReflectionEntry) == 272u,
              "ManagedServerStubReflectionEntry must remain ABI compatible with managed interop.");
static_assert(offsetof(xs::host::ManagedServerStubReflectionEntry, entity_id_utf8) >
                  offsetof(xs::host::ManagedServerStubReflectionEntry, entity_id_length),
              "ManagedServerStubReflectionEntry entity ID buffer order must remain ABI compatible.");
static_assert(sizeof(xs::host::ManagedServerStubOwnershipEntry) == 404u,
              "ManagedServerStubOwnershipEntry must remain ABI compatible with managed interop.");
static_assert(offsetof(xs::host::ManagedServerStubOwnershipEntry, owner_game_node_id_utf8) >
                  offsetof(xs::host::ManagedServerStubOwnershipEntry, owner_game_node_id_length),
              "ManagedServerStubOwnershipEntry owner node buffer order must remain ABI compatible.");
static_assert(offsetof(xs::host::ManagedServerStubOwnershipSync, assignments) >
                  offsetof(xs::host::ManagedServerStubOwnershipSync, assignment_count),
              "ManagedServerStubOwnershipSync assignments pointer order must remain ABI compatible.");
static_assert(sizeof(xs::host::ManagedServerStubReadyEntry) == 276u,
              "ManagedServerStubReadyEntry must remain ABI compatible with managed interop.");
static_assert(offsetof(xs::host::ManagedServerStubReadyEntry, entry_flags) >
                  offsetof(xs::host::ManagedServerStubReadyEntry, ready),
              "ManagedServerStubReadyEntry ready field order must remain ABI compatible.");
static_assert(static_cast<std::int32_t>(xs::host::ManagedHostErrorCode::RuntimeInitializeFailed) == 4008,
              "ManagedHostErrorCode values must remain aligned with docs/ERROR_CODE.md.");
static_assert(static_cast<std::int32_t>(xs::host::ManagedHostErrorCode::EntryPointNotBound) == 4014,
              "ManagedHostErrorCode values must remain aligned with docs/ERROR_CODE.md.");

namespace xs::host
{
}


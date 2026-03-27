#include "ManagedRuntimeHost.h"

#include <cstddef>
#include <type_traits>
#include <utility>

static_assert(std::is_default_constructible_v<xs::host::ManagedRuntimeHost>, "ManagedRuntimeHost must remain default constructible.");
static_assert(
    std::is_same_v<decltype(std::declval<const xs::host::ManagedRuntimeHost&>().IsLoaded()), bool>,
    "ManagedRuntimeHost::IsLoaded must return bool.");
static_assert(
    std::is_same_v<decltype(std::declval<const xs::host::ManagedRuntimeHost&>().AreGameExportsBound()), bool>,
    "ManagedRuntimeHost::AreGameExportsBound must return bool.");
static_assert(
    std::is_same_v<decltype(std::declval<const xs::host::ManagedRuntimeHost&>().AreServerStubCatalogExportsBound()), bool>,
    "ManagedRuntimeHost::AreServerStubCatalogExportsBound must return bool.");
static_assert(
    std::is_same_v<
        decltype(std::declval<const xs::host::ManagedRuntimeHost&>().load_assembly_and_get_function_pointer()),
        load_assembly_and_get_function_pointer_fn>,
    "ManagedRuntimeHost must expose load_assembly_and_get_function_pointer delegate type.");
static_assert(
    std::is_same_v<
        decltype(&xs::host::ManagedRuntimeHost::Load),
        xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)(const xs::host::ManagedRuntimeHostOptions&)>,
    "ManagedRuntimeHost::Load must return only ManagedHostErrorCode.");
static_assert(
    std::is_same_v<
        decltype(&xs::host::ManagedRuntimeHost::Unload),
        xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)() noexcept>,
    "ManagedRuntimeHost::Unload must return only ManagedHostErrorCode.");
static_assert(
    std::is_same_v<
        decltype(&xs::host::ManagedRuntimeHost::BindGameExports),
        xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)()>,
    "ManagedRuntimeHost::BindGameExports must return only ManagedHostErrorCode.");
static_assert(
    std::is_same_v<
        decltype(&xs::host::ManagedRuntimeHost::GetGameExports),
        xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)(xs::host::ManagedGameExports&) const noexcept>,
    "ManagedRuntimeHost::GetGameExports must return only ManagedHostErrorCode plus output exports.");
static_assert(
    std::is_same_v<
        decltype(&xs::host::ManagedRuntimeHost::BindServerStubCatalogExports),
        xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)()>,
    "ManagedRuntimeHost::BindServerStubCatalogExports must return only ManagedHostErrorCode.");
static_assert(
    std::is_same_v<
        decltype(&xs::host::ManagedRuntimeHost::GetServerStubCatalogExports),
        xs::host::ManagedHostErrorCode (xs::host::ManagedRuntimeHost::*)(xs::host::ManagedServerStubCatalogExports&) const noexcept>,
    "ManagedRuntimeHost::GetServerStubCatalogExports must return only ManagedHostErrorCode plus output exports.");
static_assert(xs::host::XS_MANAGED_ABI_VERSION == 1u, "Managed ABI version must remain aligned with docs/MANAGED_INTEROP.md.");
static_assert(
    offsetof(xs::host::ManagedInitArgs, abi_version) == 4,
    "ManagedInitArgs::abi_version offset must remain ABI compatible.");
static_assert(
    offsetof(xs::host::ManagedMessageView, payload) > offsetof(xs::host::ManagedMessageView, player_id),
    "ManagedMessageView payload field order must remain ABI compatible.");
static_assert(
    sizeof(xs::host::ManagedServerStubCatalogEntry) == 272u,
    "ManagedServerStubCatalogEntry must remain ABI compatible with managed interop.");
static_assert(
    offsetof(xs::host::ManagedServerStubCatalogEntry, entity_id_utf8) >
        offsetof(xs::host::ManagedServerStubCatalogEntry, entity_id_length),
    "ManagedServerStubCatalogEntry entity ID buffer order must remain ABI compatible.");
static_assert(
    static_cast<std::int32_t>(xs::host::ManagedHostErrorCode::RuntimeInitializeFailed) == 4008,
    "ManagedHostErrorCode values must remain aligned with docs/ERROR_CODE.md.");
static_assert(
    static_cast<std::int32_t>(xs::host::ManagedHostErrorCode::EntryPointNotBound) == 4014,
    "ManagedHostErrorCode values must remain aligned with docs/ERROR_CODE.md.");

namespace xs::host
{
}

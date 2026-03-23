#include "ManagedRuntimeHost.h"

#include <type_traits>
#include <utility>

static_assert(std::is_default_constructible_v<xs::host::ManagedRuntimeHost>, "ManagedRuntimeHost must remain default constructible.");
static_assert(
    std::is_same_v<decltype(std::declval<const xs::host::ManagedRuntimeHost&>().IsLoaded()), bool>,
    "ManagedRuntimeHost::IsLoaded must return bool.");
static_assert(
    std::is_same_v<
        decltype(std::declval<const xs::host::ManagedRuntimeHost&>().load_assembly_and_get_function_pointer()),
        load_assembly_and_get_function_pointer_fn>,
    "ManagedRuntimeHost must expose load_assembly_and_get_function_pointer delegate type.");
static_assert(
    static_cast<std::int32_t>(xs::host::ManagedHostErrorCode::RuntimeInitializeFailed) == 4008,
    "ManagedHostErrorCode values must remain aligned with docs/ERROR_CODE.md.");

namespace xs::host
{
}

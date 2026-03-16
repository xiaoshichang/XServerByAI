#include "Logging.h"

#include <type_traits>

static_assert(std::is_enum_v<xs::core::LogLevel>, "LogLevel must remain an enum.");
static_assert(std::is_enum_v<xs::core::ProcessType>, "ProcessType must remain an enum.");

namespace xs::core {
}

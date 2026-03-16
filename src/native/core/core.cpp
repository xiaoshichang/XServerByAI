#include "Config.h"
#include "Json.h"
#include "Logging.h"

#include <type_traits>

static_assert(std::is_enum_v<xs::core::LogLevel>, "LogLevel must remain an enum.");
static_assert(std::is_enum_v<xs::core::ProcessType>, "ProcessType must remain an enum.");
static_assert(std::is_enum_v<xs::core::NodeSelectorKind>, "NodeSelectorKind must remain an enum.");
static_assert(std::is_same_v<xs::core::Json, nlohmann::json>, "Json alias must remain bound to nlohmann::json.");
static_assert(
    std::is_same_v<xs::core::OrderedJson, nlohmann::ordered_json>,
    "OrderedJson alias must remain bound to nlohmann::ordered_json.");
static_assert(
    std::is_same_v<decltype(xs::core::SerializeJson(42)), xs::core::Json>,
    "SerializeJson must produce Json.");

namespace xs::core {
}

#include "Config.h"
#include "Json.h"
#include "Logging.h"
#include "TimeUtils.h"
#include "Timer.h"

#include <chrono>
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
static_assert(
    std::is_same_v<xs::core::Milliseconds, std::chrono::milliseconds>,
    "Milliseconds alias must remain bound to std::chrono::milliseconds.");
static_assert(
    std::is_same_v<decltype(xs::core::SteadyNow()), xs::core::SteadyTimePoint>,
    "SteadyNow must return a steady-clock time point.");
static_assert(
    std::is_constructible_v<xs::core::TimerManager, asio::io_context&>,
    "TimerManager must remain constructible from asio::io_context.");
static_assert(
    std::is_same_v<decltype(xs::core::IsTimerID(xs::core::TimerCreateResult{})), bool>,
    "Timer result helpers must remain available.");

namespace xs::core {
}

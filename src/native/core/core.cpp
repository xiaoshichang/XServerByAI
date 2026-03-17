#include "Config.h"
#include "CoreLoopExecutor.h"
#include "Json.h"
#include "Logging.h"
#include "MainEventLoop.h"
#include "TimeUtils.h"
#include "Timer.h"

#include <chrono>
#include <string>
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
static_assert(
    std::is_default_constructible_v<xs::core::CoreLoopExecutorOptions>,
    "CoreLoopExecutorOptions must remain default constructible.");
static_assert(
    std::is_same_v<decltype(xs::core::CurrentThreadName()), std::string>,
    "CurrentThreadName must remain available for diagnostics and tests.");
static_assert(
    std::is_constructible_v<xs::core::CoreLoopExecutor, xs::core::CoreLoopExecutorOptions>,
    "CoreLoopExecutor must remain constructible from options.");
static_assert(
    std::is_default_constructible_v<xs::core::MainEventLoopOptions>,
    "MainEventLoopOptions must remain default constructible.");
static_assert(
    std::is_default_constructible_v<xs::core::MainEventLoopTickInfo>,
    "MainEventLoopTickInfo must remain default constructible.");
static_assert(
    std::is_constructible_v<xs::core::MainEventLoop, xs::core::MainEventLoopOptions>,
    "MainEventLoop must remain constructible from options.");

namespace xs::core
{
}
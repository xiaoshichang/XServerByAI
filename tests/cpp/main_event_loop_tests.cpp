#include "CoreLoopExecutor.h"
#include "MainEventLoop.h"
#include "Timer.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace
{

int g_failures = 0;

void Check(bool condition, const char* expression, const char* message = nullptr)
{
    if (condition)
    {
        return;
    }

    std::cerr << "Check failed: " << expression;
    if (message != nullptr)
    {
        std::cerr << " (" << message << ")";
    }
    std::cerr << '\n';
    ++g_failures;
}

#define XS_CHECK(expr) Check((expr), #expr)
#define XS_CHECK_MSG(expr, message) Check((expr), #expr, (message))

void TestMainEventLoopRunsHooksOnCurrentThread()
{
    xs::core::MainEventLoop event_loop({.thread_name = "xs-main-loop1"});
    const auto caller_thread_id = std::this_thread::get_id();
    std::string start_thread_name;
    std::string stop_thread_name;
    bool start_on_caller_thread = false;
    bool stop_on_caller_thread = false;

    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [&](xs::core::MainEventLoop& running_loop, std::string* error_message) {
        if (error_message != nullptr)
        {
            error_message->clear();
        }

        start_thread_name = xs::core::CurrentThreadName();
        start_on_caller_thread = std::this_thread::get_id() == caller_thread_id;
        running_loop.RequestStop();
        return true;
    };
    hooks.on_stop = [&](xs::core::MainEventLoop&) {
        stop_thread_name = xs::core::CurrentThreadName();
        stop_on_caller_thread = std::this_thread::get_id() == caller_thread_id;
    };

    std::string error_message;
    XS_CHECK_MSG(event_loop.Run(std::move(hooks), &error_message), error_message.c_str());
    XS_CHECK(start_thread_name == "xs-main-loop1");
    XS_CHECK(stop_thread_name == "xs-main-loop1");
    XS_CHECK(start_on_caller_thread);
    XS_CHECK(stop_on_caller_thread);
    XS_CHECK(!event_loop.IsRunning());
}

void TestMainEventLoopRejectsInvalidTickConfiguration()
{
    xs::core::MainEventLoop zero_interval_loop({.thread_name = "xs-main-loop2"});
    std::string error_message{"not-cleared"};

    XS_CHECK(!zero_interval_loop.Run(
        {.on_tick = [](xs::core::MainEventLoop&, const xs::core::MainEventLoopTickInfo&) {
        }},
        &error_message));
    XS_CHECK_MSG(
        error_message.find("requires a positive tick interval") != std::string::npos,
        error_message.c_str());

    xs::core::MainEventLoop missing_callback_loop(
        {.thread_name = "xs-main-loop3", .tick_interval = std::chrono::milliseconds(1)});
    error_message = "not-cleared";
    XS_CHECK(!missing_callback_loop.Run({}, &error_message));
    XS_CHECK_MSG(
        error_message.find("requires an on_tick callback") != std::string::npos,
        error_message.c_str());

    xs::core::MainEventLoop negative_interval_loop(
        {.thread_name = "xs-main-loop4", .tick_interval = -std::chrono::milliseconds(1)});
    error_message = "not-cleared";
    XS_CHECK(!negative_interval_loop.Run({}, &error_message));
    XS_CHECK_MSG(
        error_message.find("must not be negative") != std::string::npos,
        error_message.c_str());
}

void TestMainEventLoopDrivesTicksAndPreScheduledTimers()
{
    xs::core::MainEventLoop event_loop(
        {.thread_name = "xs-main-loop-tick", .tick_interval = std::chrono::milliseconds(2)});
    bool timer_fired = false;
    std::string tick_thread_name;
    std::uint64_t last_tick_count = 0;
    xs::core::SteadyDuration last_delta = xs::core::SteadyDuration::zero();
    int tick_calls = 0;

    const auto timer_id = event_loop.timers().CreateOnce(std::chrono::milliseconds(1), [&]() {
        timer_fired = true;
    });
    XS_CHECK(xs::core::IsTimerID(timer_id));

    xs::core::MainEventLoopHooks hooks;
    hooks.on_tick = [&](xs::core::MainEventLoop& running_loop, const xs::core::MainEventLoopTickInfo& tick_info) {
        tick_thread_name = xs::core::CurrentThreadName();
        last_tick_count = tick_info.tick_count;
        last_delta = tick_info.delta;
        ++tick_calls;
        if (tick_calls == 3)
        {
            running_loop.RequestStop();
        }
    };

    std::string error_message;
    XS_CHECK_MSG(event_loop.Run(std::move(hooks), &error_message), error_message.c_str());
    XS_CHECK(timer_fired);
    XS_CHECK(tick_calls == 3);
    XS_CHECK(last_tick_count == 3);
    XS_CHECK(last_delta > xs::core::SteadyDuration::zero());
    XS_CHECK(tick_thread_name == "xs-main-loop-tick");
    XS_CHECK(event_loop.timers().size() == 0);
}

void TestMainEventLoopPropagatesStartupFailure()
{
    xs::core::MainEventLoop event_loop({.thread_name = "xs-main-loop-fail"});
    bool stop_called = false;

    xs::core::MainEventLoopHooks hooks;
    hooks.on_start = [](xs::core::MainEventLoop&, std::string* error_message) {
        if (error_message != nullptr)
        {
            *error_message = "startup failed";
        }
        return false;
    };
    hooks.on_stop = [&](xs::core::MainEventLoop&) {
        stop_called = true;
    };

    std::string error_message;
    XS_CHECK(!event_loop.Run(std::move(hooks), &error_message));
    XS_CHECK_MSG(error_message.find("startup failed") != std::string::npos, error_message.c_str());
    XS_CHECK(stop_called);
    XS_CHECK(!event_loop.IsRunning());
}

void TestMainEventLoopPropagatesTickExceptions()
{
    xs::core::MainEventLoop event_loop(
        {.thread_name = "xs-main-loop-throw", .tick_interval = std::chrono::milliseconds(1)});
    bool stop_called = false;

    xs::core::MainEventLoopHooks hooks;
    hooks.on_tick = [](xs::core::MainEventLoop&, const xs::core::MainEventLoopTickInfo&) {
        throw std::runtime_error("tick boom");
    };
    hooks.on_stop = [&](xs::core::MainEventLoop&) {
        stop_called = true;
    };

    std::string error_message;
    XS_CHECK(!event_loop.Run(std::move(hooks), &error_message));
    XS_CHECK_MSG(error_message.find("tick boom") != std::string::npos, error_message.c_str());
    XS_CHECK(stop_called);
    XS_CHECK(!event_loop.IsRunning());
}

} // namespace

int main()
{
    TestMainEventLoopRunsHooksOnCurrentThread();
    TestMainEventLoopRejectsInvalidTickConfiguration();
    TestMainEventLoopDrivesTicksAndPreScheduledTimers();
    TestMainEventLoopPropagatesStartupFailure();
    TestMainEventLoopPropagatesTickExceptions();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " main event loop test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
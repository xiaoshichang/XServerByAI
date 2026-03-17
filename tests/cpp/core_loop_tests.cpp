#include "CoreLoopExecutor.h"
#include "Timer.h"

#include <asio/post.hpp>

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>

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

template <typename T>
T WaitFutureValue(std::future<T>& future, const T& fallback, const char* name)
{
    const auto status = future.wait_for(std::chrono::seconds(2));
    XS_CHECK_MSG(status == std::future_status::ready, name);
    if (status != std::future_status::ready)
    {
        return fallback;
    }

    return future.get();
}

void WaitFuture(std::future<void>& future, const char* name)
{
    const auto status = future.wait_for(std::chrono::seconds(2));
    XS_CHECK_MSG(status == std::future_status::ready, name);
    if (status == std::future_status::ready)
    {
        future.get();
    }
}

void TestCoreLoopExecutorRunsPostedWorkOnCurrentThread()
{
    xs::core::CoreLoopExecutor executor({.thread_name = "xs-core-loop1"});
    std::promise<std::string> name_promise;
    std::future<std::string> name_future = name_promise.get_future();
    std::promise<bool> caller_thread_promise;
    std::future<bool> caller_thread_future = caller_thread_promise.get_future();
    const auto caller_thread_id = std::this_thread::get_id();

    asio::post(executor.executor(), [&executor, &name_promise, &caller_thread_promise, caller_thread_id]() mutable {
        name_promise.set_value(xs::core::CurrentThreadName());
        caller_thread_promise.set_value(std::this_thread::get_id() == caller_thread_id);
        executor.Stop();
    });

    std::string error_message;
    const bool started = executor.Start(&error_message);
    XS_CHECK_MSG(started, error_message.c_str());
    XS_CHECK(!executor.IsRunning());
    XS_CHECK(WaitFutureValue(name_future, std::string{}, "core loop executor name future") == "xs-core-loop1");
    XS_CHECK(WaitFutureValue(caller_thread_future, false, "core loop caller thread future"));
}

void TestCoreLoopExecutorRejectsEmptyName()
{
    xs::core::CoreLoopExecutor executor({.thread_name = ""});
    std::string error_message{"not-cleared"};

    const bool started = executor.Start(&error_message);

    XS_CHECK(!started);
    XS_CHECK_MSG(error_message.find("must not be empty") != std::string::npos, error_message.c_str());
    XS_CHECK(!executor.IsRunning());
}

void TestCoreLoopExecutorRejectsDoubleStart()
{
    xs::core::CoreLoopExecutor executor({.thread_name = "xs-core-loop2"});
    std::promise<bool> nested_start_promise;
    std::future<bool> nested_start_future = nested_start_promise.get_future();

    asio::post(executor.executor(), [&executor, &nested_start_promise]() mutable {
        std::string nested_error_message;
        const bool started_again = executor.Start(&nested_error_message);
        nested_start_promise.set_value(
            !started_again && nested_error_message.find("already running") != std::string::npos);
        executor.Stop();
    });

    std::string error_message;
    XS_CHECK_MSG(executor.Start(&error_message), error_message.c_str());
    XS_CHECK(WaitFutureValue(nested_start_future, false, "nested start future"));
}

void TestCoreLoopExecutorCanRestart()
{
    xs::core::CoreLoopExecutor executor({.thread_name = "xs-core-loop-retry"});
    std::string error_message;

    std::promise<void> first_promise;
    std::future<void> first_future = first_promise.get_future();
    asio::post(executor.executor(), [&executor, &first_promise]() mutable {
        first_promise.set_value();
        executor.Stop();
    });

    XS_CHECK_MSG(executor.Start(&error_message), error_message.c_str());
    WaitFuture(first_future, "first executor run");
    XS_CHECK(!executor.IsRunning());

    std::promise<std::string> second_promise;
    std::future<std::string> second_future = second_promise.get_future();
    asio::post(executor.executor(), [&executor, &second_promise]() mutable {
        second_promise.set_value(xs::core::CurrentThreadName());
        executor.Stop();
    });

    XS_CHECK_MSG(executor.Start(&error_message), error_message.c_str());
    XS_CHECK(WaitFutureValue(second_future, std::string{}, "second core loop run") == "xs-core-loop-retry");
    XS_CHECK(!executor.IsRunning());
}

void TestCoreLoopExecutorWorksWithTimerManager()
{
    xs::core::CoreLoopExecutor executor({.thread_name = "xs-core-loop-timer"});
    xs::core::TimerManager timer_manager(executor.context());
    std::promise<std::string> promise;
    std::future<std::string> future = promise.get_future();

    const auto timer_id = timer_manager.CreateOnce(std::chrono::milliseconds(10), [&executor, &promise]() mutable {
        promise.set_value(xs::core::CurrentThreadName());
        executor.Stop();
    });

    XS_CHECK(xs::core::IsTimerID(timer_id));

    std::string error_message;
    XS_CHECK_MSG(executor.Start(&error_message), error_message.c_str());
    XS_CHECK(WaitFutureValue(future, std::string{}, "timer callback future") == "xs-core-loop-timer");
    XS_CHECK(!timer_manager.Contains(timer_id));
    XS_CHECK(!executor.IsRunning());
}

} // namespace

int main()
{
    TestCoreLoopExecutorRunsPostedWorkOnCurrentThread();
    TestCoreLoopExecutorRejectsEmptyName();
    TestCoreLoopExecutorRejectsDoubleStart();
    TestCoreLoopExecutorCanRestart();
    TestCoreLoopExecutorWorksWithTimerManager();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " core loop runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
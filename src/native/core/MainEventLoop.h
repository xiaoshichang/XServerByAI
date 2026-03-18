#pragma once

#include "TimeUtils.h"
#include "Timer.h"

#include <asio/any_io_executor.hpp>
#include <asio/io_context.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace xs::core
{

class MainEventLoop;

struct MainEventLoopOptions
{
    std::string thread_name{"xs-main-loop"};
    SteadyDuration tick_interval{SteadyDuration::zero()};
};

enum class MainEventLoopErrorCode : std::uint8_t
{
    None = 0,
    EmptyThreadName,
    NegativeTickInterval,
    TickCallbackRequiresPositiveInterval,
    TickIntervalRequiresCallback,
    AlreadyRunning,
    ExecutorStartFailed,
    StartupCallbackFailed,
    TickTimerCreateFailed,
    TickCallbackThrew,
    StopCallbackThrew,
    InvalidState,
};

struct MainEventLoopTickInfo
{
    SteadyTimePoint now{};
    SteadyDuration delta{SteadyDuration::zero()};
    std::uint64_t tick_count{0};
};

[[nodiscard]] std::string_view MainEventLoopErrorMessage(MainEventLoopErrorCode code) noexcept;

using MainEventLoopStartCallback = std::function<MainEventLoopErrorCode(MainEventLoop&, std::string* error_message)>;
using MainEventLoopTickCallback = std::function<void(MainEventLoop&, const MainEventLoopTickInfo&)>;
using MainEventLoopStopCallback = std::function<void(MainEventLoop&)>;

struct MainEventLoopHooks
{
    MainEventLoopStartCallback on_start{};
    MainEventLoopTickCallback on_tick{};
    MainEventLoopStopCallback on_stop{};
};

class MainEventLoop final
{
  public:
    explicit MainEventLoop(MainEventLoopOptions options = {});
    ~MainEventLoop();

    MainEventLoop(const MainEventLoop&) = delete;
    MainEventLoop& operator=(const MainEventLoop&) = delete;
    MainEventLoop(MainEventLoop&&) = delete;
    MainEventLoop& operator=(MainEventLoop&&) = delete;

    // Owner-thread only. Runs the event loop on the current thread until RequestStop() is requested.
    [[nodiscard]] MainEventLoopErrorCode Run(MainEventLoopHooks hooks = {}, std::string* error_message = nullptr);
    // Owner-thread only. Requests the active Run() call on the same thread to exit the event loop.
    void RequestStop() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] const MainEventLoopOptions& options() const noexcept;
    [[nodiscard]] asio::any_io_executor executor() noexcept;
    [[nodiscard]] asio::io_context& context() noexcept;
    [[nodiscard]] TimerManager& timers() noexcept;
    [[nodiscard]] const TimerManager& timers() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::core

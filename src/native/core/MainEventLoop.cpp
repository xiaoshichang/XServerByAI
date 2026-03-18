#include "MainEventLoop.h"

#include "CoreLoopExecutor.h"

#include <asio/post.hpp>

#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace xs::core
{
namespace
{

void ClearError(std::string* error_message)
{
    if (error_message != nullptr)
    {
        error_message->clear();
    }
}

MainEventLoopErrorCode SetError(
    MainEventLoopErrorCode code,
    std::string message,
    std::string* error_message)
{
    if (error_message != nullptr)
    {
        *error_message = std::move(message);
    }
    return code;
}

CoreLoopExecutorOptions ToExecutorOptions(const MainEventLoopOptions& options)
{
    CoreLoopExecutorOptions executor_options;
    executor_options.thread_name = options.thread_name;
    return executor_options;
}

enum class EventLoopState
{
    stopped,
    running,
    stopping,
};

struct RunState final
{
    explicit RunState(MainEventLoopHooks run_hooks)
        : hooks(std::move(run_hooks))
    {
    }

    MainEventLoopHooks hooks{};
    std::string startup_error{};
    std::string runtime_error{};
    bool bootstrap_started{false};
    MainEventLoopErrorCode startup_result{MainEventLoopErrorCode::None};
    MainEventLoopErrorCode runtime_result{MainEventLoopErrorCode::None};
    std::optional<SteadyTimePoint> last_tick_time{};
    std::uint64_t tick_count{0};
};

std::string DescribeTickTimerCreateFailure(TimerCreateResult create_result)
{
    const auto error_code = TimerErrorFromCreateResult(create_result);
    return std::string("Failed to create main event loop tick timer: ") + std::string(TimerErrorMessage(error_code));
}

} // namespace

std::string_view MainEventLoopErrorMessage(MainEventLoopErrorCode code) noexcept
{
    switch (code)
    {
    case MainEventLoopErrorCode::None:
        return "No error.";
    case MainEventLoopErrorCode::EmptyThreadName:
        return "Main event loop thread name must not be empty.";
    case MainEventLoopErrorCode::NegativeTickInterval:
        return "Main event loop tick interval must not be negative.";
    case MainEventLoopErrorCode::TickCallbackRequiresPositiveInterval:
        return "Main event loop tick callback requires a positive tick interval.";
    case MainEventLoopErrorCode::TickIntervalRequiresCallback:
        return "Main event loop tick interval requires an on_tick callback.";
    case MainEventLoopErrorCode::AlreadyRunning:
        return "Main event loop is already running.";
    case MainEventLoopErrorCode::ExecutorStartFailed:
        return "Failed to start the main event loop executor.";
    case MainEventLoopErrorCode::StartupCallbackFailed:
        return "Main event loop startup callback failed.";
    case MainEventLoopErrorCode::TickTimerCreateFailed:
        return "Failed to create the main event loop tick timer.";
    case MainEventLoopErrorCode::TickCallbackThrew:
        return "Main event loop tick callback threw an exception.";
    case MainEventLoopErrorCode::StopCallbackThrew:
        return "Main event loop stop callback threw an exception.";
    case MainEventLoopErrorCode::InvalidState:
        return "Main event loop is in an invalid state.";
    }

    return "Unknown main event loop error.";
}

class MainEventLoop::Impl final
{
  public:
    explicit Impl(MainEventLoopOptions options)
        : options_(std::move(options)),
          executor_(ToExecutorOptions(options_)),
          timer_manager_(executor_.context())
    {
    }

    MainEventLoopErrorCode Run(MainEventLoop& owner, MainEventLoopHooks hooks, std::string* error_message)
    {
        ClearError(error_message);

        const MainEventLoopErrorCode validation_result = ValidateOptionsAndHooks(hooks, error_message);
        if (validation_result != MainEventLoopErrorCode::None)
        {
            return validation_result;
        }

        if (state_ != EventLoopState::stopped)
        {
            return SetError(
                MainEventLoopErrorCode::AlreadyRunning,
                "Main event loop is already running.",
                error_message);
        }

        state_ = EventLoopState::running;

        auto run_state = std::make_shared<RunState>(std::move(hooks));
        asio::post(executor_.executor(), [this, &owner, run_state]() mutable {
            Bootstrap(owner, run_state);
        });

        std::string executor_error;
        const CoreLoopErrorCode executor_result = executor_.Start(&executor_error);

        timer_manager_.CancelAll();
        state_ = EventLoopState::stopped;

        std::string stop_error;
        MainEventLoopErrorCode stop_result = MainEventLoopErrorCode::None;
        if (run_state->bootstrap_started && run_state->hooks.on_stop)
        {
            try
            {
                run_state->hooks.on_stop(owner);
            }
            catch (const std::exception& exception)
            {
                stop_result = MainEventLoopErrorCode::StopCallbackThrew;
                stop_error = std::string("Main event loop stop callback threw: ") + exception.what();
            }
            catch (...)
            {
                stop_result = MainEventLoopErrorCode::StopCallbackThrew;
                stop_error = "Main event loop stop callback threw an unknown exception.";
            }
        }

        if (executor_result != CoreLoopErrorCode::None)
        {
            if (executor_error.empty())
            {
                executor_error = std::string("Failed to start main event loop executor: ") +
                                 std::string(CoreLoopErrorMessage(executor_result));
            }

            return SetError(MainEventLoopErrorCode::ExecutorStartFailed, std::move(executor_error), error_message);
        }

        if (run_state->startup_result != MainEventLoopErrorCode::None)
        {
            if (run_state->startup_error.empty())
            {
                run_state->startup_error = std::string(MainEventLoopErrorMessage(run_state->startup_result));
            }

            return SetError(run_state->startup_result, std::move(run_state->startup_error), error_message);
        }

        if (run_state->runtime_result != MainEventLoopErrorCode::None)
        {
            if (run_state->runtime_error.empty())
            {
                run_state->runtime_error = std::string(MainEventLoopErrorMessage(run_state->runtime_result));
            }

            return SetError(run_state->runtime_result, std::move(run_state->runtime_error), error_message);
        }

        if (stop_result != MainEventLoopErrorCode::None)
        {
            if (stop_error.empty())
            {
                stop_error = std::string(MainEventLoopErrorMessage(stop_result));
            }

            return SetError(stop_result, std::move(stop_error), error_message);
        }

        ClearError(error_message);
        return MainEventLoopErrorCode::None;
    }

    void RequestStop() noexcept
    {
        if (state_ == EventLoopState::stopped)
        {
            return;
        }

        state_ = EventLoopState::stopping;
        executor_.Stop();
    }

    [[nodiscard]] bool IsRunning() const noexcept
    {
        return state_ != EventLoopState::stopped;
    }

    [[nodiscard]] const MainEventLoopOptions& options() const noexcept
    {
        return options_;
    }

    [[nodiscard]] asio::any_io_executor executor() noexcept
    {
        return executor_.executor();
    }

    [[nodiscard]] asio::io_context& context() noexcept
    {
        return executor_.context();
    }

    [[nodiscard]] TimerManager& timers() noexcept
    {
        return timer_manager_;
    }

    [[nodiscard]] const TimerManager& timers() const noexcept
    {
        return timer_manager_;
    }

  private:
    MainEventLoopErrorCode ValidateOptionsAndHooks(
        const MainEventLoopHooks& hooks,
        std::string* error_message) const
    {
        if (options_.thread_name.empty())
        {
            return SetError(
                MainEventLoopErrorCode::EmptyThreadName,
                "Main event loop thread name must not be empty.",
                error_message);
        }

        if (options_.tick_interval < SteadyDuration::zero())
        {
            return SetError(
                MainEventLoopErrorCode::NegativeTickInterval,
                "Main event loop tick interval must not be negative.",
                error_message);
        }

        if (hooks.on_tick && options_.tick_interval <= SteadyDuration::zero())
        {
            return SetError(
                MainEventLoopErrorCode::TickCallbackRequiresPositiveInterval,
                "Main event loop tick callback requires a positive tick interval.",
                error_message);
        }

        if (!hooks.on_tick && options_.tick_interval > SteadyDuration::zero())
        {
            return SetError(
                MainEventLoopErrorCode::TickIntervalRequiresCallback,
                "Main event loop tick interval requires an on_tick callback.",
                error_message);
        }

        ClearError(error_message);
        return MainEventLoopErrorCode::None;
    }

    void Bootstrap(MainEventLoop& owner, const std::shared_ptr<RunState>& run_state)
    {
        run_state->bootstrap_started = true;

        try
        {
            if (run_state->hooks.on_start)
            {
                const MainEventLoopErrorCode start_result = run_state->hooks.on_start(owner, &run_state->startup_error);
                if (start_result != MainEventLoopErrorCode::None)
                {
                    run_state->startup_result = start_result;
                    if (run_state->startup_error.empty())
                    {
                        run_state->startup_error = std::string(MainEventLoopErrorMessage(start_result));
                    }
                    RequestStop();
                    return;
                }
            }

            if (!run_state->hooks.on_tick)
            {
                return;
            }

            run_state->last_tick_time = SteadyNow();
            const auto create_result = timer_manager_.CreateRepeating(options_.tick_interval, [this, &owner, run_state]() mutable {
                HandleTick(owner, run_state);
            });
            if (!IsTimerID(create_result))
            {
                run_state->startup_result = MainEventLoopErrorCode::TickTimerCreateFailed;
                run_state->startup_error = DescribeTickTimerCreateFailure(create_result);
                RequestStop();
            }
        }
        catch (const std::exception& exception)
        {
            run_state->startup_result = MainEventLoopErrorCode::StartupCallbackFailed;
            run_state->startup_error = std::string("Main event loop startup callback threw: ") + exception.what();
            RequestStop();
        }
        catch (...)
        {
            run_state->startup_result = MainEventLoopErrorCode::StartupCallbackFailed;
            run_state->startup_error = "Main event loop startup callback threw an unknown exception.";
            RequestStop();
        }
    }

    void HandleTick(MainEventLoop& owner, const std::shared_ptr<RunState>& run_state)
    {
        if (run_state->runtime_result != MainEventLoopErrorCode::None || !run_state->hooks.on_tick)
        {
            return;
        }

        const auto now = SteadyNow();
        MainEventLoopTickInfo tick_info;
        tick_info.now = now;
        tick_info.delta = run_state->last_tick_time.has_value() ? (now - *run_state->last_tick_time) : options_.tick_interval;
        tick_info.tick_count = ++run_state->tick_count;
        run_state->last_tick_time = now;

        try
        {
            run_state->hooks.on_tick(owner, tick_info);
        }
        catch (const std::exception& exception)
        {
            run_state->runtime_result = MainEventLoopErrorCode::TickCallbackThrew;
            run_state->runtime_error = std::string("Main event loop tick callback threw: ") + exception.what();
            RequestStop();
        }
        catch (...)
        {
            run_state->runtime_result = MainEventLoopErrorCode::TickCallbackThrew;
            run_state->runtime_error = "Main event loop tick callback threw an unknown exception.";
            RequestStop();
        }
    }

    MainEventLoopOptions options_{};
    CoreLoopExecutor executor_;
    TimerManager timer_manager_;
    EventLoopState state_{EventLoopState::stopped};
};

MainEventLoop::MainEventLoop(MainEventLoopOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

MainEventLoop::~MainEventLoop()
{
    RequestStop();
}

MainEventLoopErrorCode MainEventLoop::Run(MainEventLoopHooks hooks, std::string* error_message)
{
    if (impl_ == nullptr)
    {
        return SetError(
            MainEventLoopErrorCode::InvalidState,
            std::string(MainEventLoopErrorMessage(MainEventLoopErrorCode::InvalidState)),
            error_message);
    }

    return impl_->Run(*this, std::move(hooks), error_message);
}

void MainEventLoop::RequestStop() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->RequestStop();
    }
}

bool MainEventLoop::IsRunning() const noexcept
{
    return impl_ != nullptr && impl_->IsRunning();
}

const MainEventLoopOptions& MainEventLoop::options() const noexcept
{
    return impl_->options();
}

asio::any_io_executor MainEventLoop::executor() noexcept
{
    return impl_->executor();
}

asio::io_context& MainEventLoop::context() noexcept
{
    return impl_->context();
}

TimerManager& MainEventLoop::timers() noexcept
{
    return impl_->timers();
}

const TimerManager& MainEventLoop::timers() const noexcept
{
    return impl_->timers();
}

} // namespace xs::core

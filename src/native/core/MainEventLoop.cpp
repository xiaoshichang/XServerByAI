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

bool SetError(std::string message, std::string* error_message)
{
    if (error_message != nullptr)
    {
        *error_message = std::move(message);
    }
    return false;
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
    bool startup_failed{false};
    bool runtime_failed{false};
    std::optional<SteadyTimePoint> last_tick_time{};
    std::uint64_t tick_count{0};
};

std::string DescribeTickTimerCreateFailure(TimerCreateResult create_result)
{
    const auto error_code = TimerErrorFromCreateResult(create_result);
    return std::string("Failed to create main event loop tick timer: ") + std::string(TimerErrorMessage(error_code));
}

} // namespace

class MainEventLoop::Impl final
{
  public:
    explicit Impl(MainEventLoopOptions options)
        : options_(std::move(options)),
          executor_(ToExecutorOptions(options_)),
          timer_manager_(executor_.context())
    {
    }

    bool Run(MainEventLoop& owner, MainEventLoopHooks hooks, std::string* error_message)
    {
        ClearError(error_message);
        if (!ValidateOptionsAndHooks(hooks, error_message))
        {
            return false;
        }

        if (state_ != EventLoopState::stopped)
        {
            return SetError("Main event loop is already running.", error_message);
        }

        state_ = EventLoopState::running;

        auto run_state = std::make_shared<RunState>(std::move(hooks));
        asio::post(executor_.executor(), [this, &owner, run_state]() mutable {
            Bootstrap(owner, run_state);
        });

        std::string executor_error;
        const bool started = executor_.Start(&executor_error);

        timer_manager_.CancelAll();
        state_ = EventLoopState::stopped;

        std::string stop_error;
        if (run_state->bootstrap_started && run_state->hooks.on_stop)
        {
            try
            {
                run_state->hooks.on_stop(owner);
            }
            catch (const std::exception& exception)
            {
                stop_error = std::string("Main event loop stop callback threw: ") + exception.what();
            }
            catch (...)
            {
                stop_error = "Main event loop stop callback threw an unknown exception.";
            }
        }

        if (!started)
        {
            return SetError(std::move(executor_error), error_message);
        }

        if (run_state->startup_failed)
        {
            return SetError(
                run_state->startup_error.empty() ? std::string("Main event loop startup failed.") : run_state->startup_error,
                error_message);
        }

        if (run_state->runtime_failed)
        {
            return SetError(
                run_state->runtime_error.empty() ? std::string("Main event loop runtime failed.") : run_state->runtime_error,
                error_message);
        }

        if (!stop_error.empty())
        {
            return SetError(std::move(stop_error), error_message);
        }

        ClearError(error_message);
        return true;
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
    bool ValidateOptionsAndHooks(const MainEventLoopHooks& hooks, std::string* error_message) const
    {
        if (options_.thread_name.empty())
        {
            return SetError("Main event loop thread name must not be empty.", error_message);
        }

        if (options_.tick_interval < SteadyDuration::zero())
        {
            return SetError("Main event loop tick interval must not be negative.", error_message);
        }

        if (hooks.on_tick && options_.tick_interval <= SteadyDuration::zero())
        {
            return SetError("Main event loop tick callback requires a positive tick interval.", error_message);
        }

        if (!hooks.on_tick && options_.tick_interval > SteadyDuration::zero())
        {
            return SetError("Main event loop tick interval requires an on_tick callback.", error_message);
        }

        ClearError(error_message);
        return true;
    }

    void Bootstrap(MainEventLoop& owner, const std::shared_ptr<RunState>& run_state)
    {
        run_state->bootstrap_started = true;

        try
        {
            if (run_state->hooks.on_start && !run_state->hooks.on_start(owner, &run_state->startup_error))
            {
                if (run_state->startup_error.empty())
                {
                    run_state->startup_error = "Main event loop startup callback failed.";
                }
                run_state->startup_failed = true;
                RequestStop();
                return;
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
                run_state->startup_error = DescribeTickTimerCreateFailure(create_result);
                run_state->startup_failed = true;
                RequestStop();
            }
        }
        catch (const std::exception& exception)
        {
            run_state->startup_error = std::string("Main event loop startup callback threw: ") + exception.what();
            run_state->startup_failed = true;
            RequestStop();
        }
        catch (...)
        {
            run_state->startup_error = "Main event loop startup callback threw an unknown exception.";
            run_state->startup_failed = true;
            RequestStop();
        }
    }

    void HandleTick(MainEventLoop& owner, const std::shared_ptr<RunState>& run_state)
    {
        if (run_state->runtime_failed || !run_state->hooks.on_tick)
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
            run_state->runtime_error = std::string("Main event loop tick callback threw: ") + exception.what();
            run_state->runtime_failed = true;
            RequestStop();
        }
        catch (...)
        {
            run_state->runtime_error = "Main event loop tick callback threw an unknown exception.";
            run_state->runtime_failed = true;
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

bool MainEventLoop::Run(MainEventLoopHooks hooks, std::string* error_message)
{
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
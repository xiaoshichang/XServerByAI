#include "Timer.h"

#include <asio/error.hpp>
#include <asio/steady_timer.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <utility>

namespace xs::core
{
namespace
{

[[nodiscard]] constexpr TimerCreateResult ToCreateResult(TimerErrorCode error_code) noexcept
{
    return static_cast<TimerCreateResult>(error_code);
}

[[nodiscard]] bool IsKnownErrorCodeValue(TimerCreateResult value) noexcept
{
    switch (static_cast<TimerErrorCode>(value))
    {
    case TimerErrorCode::None:
    case TimerErrorCode::InvalidTimerID:
    case TimerErrorCode::TimerNotFound:
    case TimerErrorCode::CallbackEmpty:
    case TimerErrorCode::IntervalMustBePositive:
    case TimerErrorCode::TimerIdExhausted:
    case TimerErrorCode::Unknown:
        return true;
    }

    return false;
}

} // namespace

bool IsTimerID(TimerCreateResult value) noexcept
{
    return value > 0;
}

TimerErrorCode TimerErrorFromCreateResult(TimerCreateResult value) noexcept
{
    if (IsTimerID(value))
    {
        return TimerErrorCode::None;
    }

    if (IsKnownErrorCodeValue(value))
    {
        return static_cast<TimerErrorCode>(value);
    }

    return TimerErrorCode::Unknown;
}

std::string_view TimerErrorMessage(TimerErrorCode error_code) noexcept
{
    switch (error_code)
    {
    case TimerErrorCode::None:
        return "Success.";
    case TimerErrorCode::InvalidTimerID:
        return "Timer ID must be greater than zero.";
    case TimerErrorCode::TimerNotFound:
        return "Timer ID was not found.";
    case TimerErrorCode::CallbackEmpty:
        return "Timer callback must not be empty.";
    case TimerErrorCode::IntervalMustBePositive:
        return "Repeating timer interval must be greater than zero.";
    case TimerErrorCode::TimerIdExhausted:
        return "Timer ID space is exhausted.";
    case TimerErrorCode::Unknown:
        return "Unknown timer error.";
    }

    return "Unknown timer error.";
}

class TimerManager::Impl final : public std::enable_shared_from_this<Impl>
{
  public:
    explicit Impl(asio::io_context& io_context)
        : io_context_(io_context)
    {
    }

    [[nodiscard]] TimerCreateResult CreateOnce(SteadyDuration delay, TimerCallback callback)
    {
        return CreateTimer(ClampNonNegativeDuration(delay), SteadyDuration::zero(), false, std::move(callback));
    }

    [[nodiscard]] TimerCreateResult CreateRepeating(SteadyDuration interval, TimerCallback callback)
    {
        if (interval <= SteadyDuration::zero())
        {
            return ToCreateResult(TimerErrorCode::IntervalMustBePositive);
        }

        return CreateTimer(interval, interval, true, std::move(callback));
    }

    [[nodiscard]] TimerErrorCode ResetOnce(TimerID timer_id, SteadyDuration delay, TimerCallback callback)
    {
        return ResetTimer(timer_id, ClampNonNegativeDuration(delay), SteadyDuration::zero(), false, std::move(callback));
    }

    [[nodiscard]] TimerErrorCode ResetRepeating(TimerID timer_id, SteadyDuration interval, TimerCallback callback)
    {
        if (interval <= SteadyDuration::zero())
        {
            return TimerErrorCode::IntervalMustBePositive;
        }

        return ResetTimer(timer_id, interval, interval, true, std::move(callback));
    }

    [[nodiscard]] TimerErrorCode Cancel(TimerID timer_id) noexcept
    {
        if (!IsTimerID(timer_id))
        {
            return TimerErrorCode::InvalidTimerID;
        }

        const auto iterator = timers_.find(timer_id);
        if (iterator == timers_.end())
        {
            return TimerErrorCode::TimerNotFound;
        }

        StopTimer(iterator->second);
        timers_.erase(iterator);
        return TimerErrorCode::None;
    }

    void CancelAll() noexcept
    {
        auto timers = std::move(timers_);
        timers_.clear();

        for (auto& [timer_id, timer] : timers)
        {
            (void)timer_id;
            StopTimer(timer);
        }
    }

    [[nodiscard]] bool Contains(TimerID timer_id) const noexcept
    {
        return timers_.find(timer_id) != timers_.end();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return timers_.size();
    }

  private:
    struct TimerEntry final
    {
        explicit TimerEntry(asio::io_context& io_context)
            : timer(io_context)
        {
        }

        asio::steady_timer timer;
        TimerCallback callback{};
        SteadyDuration interval{SteadyDuration::zero()};
        bool repeating{false};
        std::uint64_t generation{0};
    };

    [[nodiscard]] TimerCreateResult CreateTimer(
        SteadyDuration first_delay,
        SteadyDuration interval,
        bool repeating,
        TimerCallback callback)
    {
        if (!callback)
        {
            return ToCreateResult(TimerErrorCode::CallbackEmpty);
        }

        if (next_timer_id_ <= 0 || next_timer_id_ == std::numeric_limits<TimerID>::max())
        {
            return ToCreateResult(TimerErrorCode::TimerIdExhausted);
        }

        const TimerID timer_id = next_timer_id_++;
        auto timer = std::make_shared<TimerEntry>(io_context_);
        timer->callback = std::move(callback);
        timer->interval = interval;
        timer->repeating = repeating;
        timer->generation = 1;

        timers_.emplace(timer_id, timer);
        ArmTimer(timer_id, timer, first_delay, timer->generation);
        return timer_id;
    }

    [[nodiscard]] TimerErrorCode ResetTimer(
        TimerID timer_id,
        SteadyDuration first_delay,
        SteadyDuration interval,
        bool repeating,
        TimerCallback callback)
    {
        if (!IsTimerID(timer_id))
        {
            return TimerErrorCode::InvalidTimerID;
        }

        const auto iterator = timers_.find(timer_id);
        if (iterator == timers_.end())
        {
            return TimerErrorCode::TimerNotFound;
        }

        if (!callback)
        {
            return TimerErrorCode::CallbackEmpty;
        }

        const auto& timer = iterator->second;
        ++timer->generation;
        timer->callback = std::move(callback);
        timer->interval = interval;
        timer->repeating = repeating;
        StopTimerWait(timer);
        ArmTimer(timer_id, timer, first_delay, timer->generation);
        return TimerErrorCode::None;
    }

    void ArmTimer(
        TimerID timer_id,
        const std::shared_ptr<TimerEntry>& timer,
        SteadyDuration delay,
        std::uint64_t generation_snapshot)
    {
        const std::weak_ptr<Impl> weak_self = weak_from_this();
        timer->timer.expires_after(ClampNonNegativeDuration(delay));
        timer->timer.async_wait([weak_self, timer, timer_id, generation_snapshot](const std::error_code& error_code) {
            const auto self = weak_self.lock();
            if (self == nullptr)
            {
                return;
            }

            self->HandleWait(timer_id, timer, generation_snapshot, error_code);
        });
    }

    void HandleWait(
        TimerID timer_id,
        const std::shared_ptr<TimerEntry>& timer,
        std::uint64_t generation_snapshot,
        const std::error_code& error_code)
    {
        if (error_code == asio::error::operation_aborted)
        {
            return;
        }

        auto iterator = timers_.find(timer_id);
        if (iterator == timers_.end() || iterator->second.get() != timer.get() || timer->generation != generation_snapshot)
        {
            return;
        }

        if (error_code || !timer->callback)
        {
            timers_.erase(iterator);
            return;
        }

        TimerCallback callback = timer->callback;
        callback();

        iterator = timers_.find(timer_id);
        if (iterator == timers_.end() || iterator->second.get() != timer.get() || timer->generation != generation_snapshot)
        {
            return;
        }

        if (!timer->repeating)
        {
            timers_.erase(iterator);
            return;
        }

        ArmTimer(timer_id, timer, timer->interval, generation_snapshot);
    }

    static void StopTimerWait(const std::shared_ptr<TimerEntry>& timer) noexcept
    {
        try
        {
            timer->timer.cancel();
        }
        catch (...)
        {
        }
    }

    static void StopTimer(const std::shared_ptr<TimerEntry>& timer) noexcept
    {
        ++timer->generation;
        timer->repeating = false;
        timer->interval = SteadyDuration::zero();
        timer->callback = {};
        StopTimerWait(timer);
    }

    asio::io_context& io_context_;
    std::unordered_map<TimerID, std::shared_ptr<TimerEntry>> timers_;
    TimerID next_timer_id_{1};
};

TimerManager::TimerManager(asio::io_context& io_context)
    : impl_(std::make_shared<Impl>(io_context))
{
}

TimerManager::~TimerManager()
{
    CancelAll();
    impl_.reset();
}

TimerCreateResult TimerManager::CreateOnce(SteadyDuration delay, TimerCallback callback)
{
    return impl_->CreateOnce(delay, std::move(callback));
}

TimerCreateResult TimerManager::CreateRepeating(SteadyDuration interval, TimerCallback callback)
{
    return impl_->CreateRepeating(interval, std::move(callback));
}

TimerErrorCode TimerManager::ResetOnce(TimerID timer_id, SteadyDuration delay, TimerCallback callback)
{
    return impl_->ResetOnce(timer_id, delay, std::move(callback));
}

TimerErrorCode TimerManager::ResetRepeating(TimerID timer_id, SteadyDuration interval, TimerCallback callback)
{
    return impl_->ResetRepeating(timer_id, interval, std::move(callback));
}

TimerErrorCode TimerManager::Cancel(TimerID timer_id) noexcept
{
    return impl_ != nullptr ? impl_->Cancel(timer_id) : TimerErrorCode::TimerNotFound;
}

void TimerManager::CancelAll() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->CancelAll();
    }
}

bool TimerManager::Contains(TimerID timer_id) const noexcept
{
    return impl_ != nullptr && impl_->Contains(timer_id);
}

std::size_t TimerManager::size() const noexcept
{
    return impl_ != nullptr ? impl_->size() : 0U;
}

} // namespace xs::core

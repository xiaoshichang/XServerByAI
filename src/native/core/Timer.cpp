#include "Timer.h"

#include <asio/error.hpp>
#include <asio/steady_timer.hpp>

#include <cstdint>
#include <utility>

namespace xs::core {
namespace {

void ClearError(std::string* error_message) {
    if (error_message != nullptr) {
        error_message->clear();
    }
}

bool SetError(const char* message, std::string* error_message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
    return false;
}

} // namespace

class SteadyTimer::State final : public std::enable_shared_from_this<State> {
public:
    explicit State(asio::io_context& io_context)
        : timer(io_context) {
    }

    bool StartOnce(SteadyDuration delay, TimerCallback callback, std::string* error_message) {
        return StartImpl(ClampNonNegativeDuration(delay), SteadyDuration::zero(), false, std::move(callback), error_message);
    }

    bool StartRepeating(SteadyDuration interval, TimerCallback callback, std::string* error_message) {
        if (interval <= SteadyDuration::zero()) {
            return SetError("Repeating timer interval must be greater than zero.", error_message);
        }

        return StartImpl(interval, interval, true, std::move(callback), error_message);
    }

    void Cancel() noexcept {
        ++generation;
        active = false;
        repeating = false;
        interval = SteadyDuration::zero();
        callback = {};
        timer.cancel();
    }

    [[nodiscard]] bool IsActive() const noexcept {
        return active;
    }

    [[nodiscard]] bool IsRepeating() const noexcept {
        return active && repeating;
    }

    [[nodiscard]] SteadyDuration Interval() const noexcept {
        return active ? interval : SteadyDuration::zero();
    }

private:
    bool StartImpl(
        SteadyDuration first_delay,
        SteadyDuration repeating_interval,
        bool repeating_mode,
        TimerCallback next_callback,
        std::string* error_message) {
        if (!next_callback) {
            return SetError("Timer callback must not be empty.", error_message);
        }

        ++generation;
        active = true;
        repeating = repeating_mode;
        interval = repeating_interval;
        callback = std::move(next_callback);

        Arm(first_delay, generation);
        ClearError(error_message);
        return true;
    }

    void Arm(SteadyDuration delay, std::uint64_t generation_snapshot) {
        const auto self = shared_from_this();
        timer.expires_after(delay);
        timer.async_wait([self, generation_snapshot](const std::error_code& error_code) {
            self->HandleWait(generation_snapshot, error_code);
        });
    }

    void HandleWait(std::uint64_t generation_snapshot, const std::error_code& error_code) {
        if (error_code == asio::error::operation_aborted) {
            return;
        }

        if (error_code || !active || generation != generation_snapshot) {
            active = false;
            repeating = false;
            interval = SteadyDuration::zero();
            callback = {};
            return;
        }

        TimerCallback current_callback = callback;
        if (!current_callback) {
            active = false;
            repeating = false;
            interval = SteadyDuration::zero();
            return;
        }

        current_callback();

        if (!active || generation != generation_snapshot) {
            return;
        }

        if (!repeating) {
            active = false;
            interval = SteadyDuration::zero();
            callback = {};
            return;
        }

        Arm(interval, generation_snapshot);
    }

    asio::steady_timer timer;
    TimerCallback callback{};
    SteadyDuration interval{SteadyDuration::zero()};
    bool active{false};
    bool repeating{false};
    std::uint64_t generation{0};
};

SteadyTimer::SteadyTimer(asio::io_context& io_context)
    : state_(std::make_shared<State>(io_context)) {
}

SteadyTimer::~SteadyTimer() {
    Cancel();
    state_.reset();
}

bool SteadyTimer::StartOnce(SteadyDuration delay, TimerCallback callback, std::string* error_message) {
    return state_->StartOnce(delay, std::move(callback), error_message);
}

bool SteadyTimer::StartRepeating(SteadyDuration interval, TimerCallback callback, std::string* error_message) {
    return state_->StartRepeating(interval, std::move(callback), error_message);
}

void SteadyTimer::Cancel() noexcept {
    if (state_ != nullptr) {
        state_->Cancel();
    }
}

bool SteadyTimer::IsActive() const noexcept {
    return state_ != nullptr && state_->IsActive();
}

bool SteadyTimer::IsRepeating() const noexcept {
    return state_ != nullptr && state_->IsRepeating();
}

SteadyDuration SteadyTimer::interval() const noexcept {
    return state_ != nullptr ? state_->Interval() : SteadyDuration::zero();
}

} // namespace xs::core

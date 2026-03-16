#include "TimeUtils.h"

namespace xs::core {

SteadyTimePoint SteadyNow() noexcept {
    return SteadyClock::now();
}

SystemTimePoint UtcNow() noexcept {
    return SystemClock::now();
}

SteadyDuration ClampNonNegativeDuration(SteadyDuration duration) noexcept {
    return duration < SteadyDuration::zero() ? SteadyDuration::zero() : duration;
}

SteadyTimePoint SteadyAfter(SteadyDuration delay, SteadyTimePoint base_time) noexcept {
    return base_time + ClampNonNegativeDuration(delay);
}

SteadyTimePoint SteadyDeadlineAfter(SteadyDuration delay) noexcept {
    return SteadyAfter(delay, SteadyNow());
}

Milliseconds RemainingMilliseconds(SteadyTimePoint deadline, SteadyTimePoint now) noexcept {
    if (deadline <= now) {
        return Milliseconds::zero();
    }

    return std::chrono::duration_cast<Milliseconds>(deadline - now);
}

std::int64_t ToUnixTimeMilliseconds(SystemTimePoint time_point) noexcept {
    return DurationToMilliseconds(time_point.time_since_epoch());
}

} // namespace xs::core

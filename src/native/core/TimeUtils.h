#pragma once

#include <chrono>
#include <cstdint>

namespace xs::core {

using SteadyClock = std::chrono::steady_clock;
using SteadyTimePoint = SteadyClock::time_point;
using SteadyDuration = SteadyClock::duration;
using SystemClock = std::chrono::system_clock;
using SystemTimePoint = SystemClock::time_point;
using Milliseconds = std::chrono::milliseconds;

[[nodiscard]] SteadyTimePoint SteadyNow() noexcept;
[[nodiscard]] SystemTimePoint UtcNow() noexcept;
[[nodiscard]] SteadyDuration ClampNonNegativeDuration(SteadyDuration duration) noexcept;
[[nodiscard]] SteadyTimePoint SteadyAfter(SteadyDuration delay, SteadyTimePoint base_time) noexcept;
[[nodiscard]] SteadyTimePoint SteadyDeadlineAfter(SteadyDuration delay) noexcept;
[[nodiscard]] Milliseconds RemainingMilliseconds(SteadyTimePoint deadline, SteadyTimePoint now) noexcept;
[[nodiscard]] std::int64_t ToUnixTimeMilliseconds(SystemTimePoint time_point) noexcept;

template <typename Rep, typename Period>
[[nodiscard]] constexpr std::int64_t DurationToMilliseconds(std::chrono::duration<Rep, Period> duration) noexcept {
    return std::chrono::duration_cast<Milliseconds>(duration).count();
}

} // namespace xs::core

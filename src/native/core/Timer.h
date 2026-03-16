#pragma once

#include "TimeUtils.h"

#include <asio/io_context.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace xs::core {

using TimerID = std::int64_t;
using TimerCreateResult = std::int64_t;
using TimerCallback = std::function<void()>;

enum class TimerErrorCode : TimerCreateResult {
    None = 0,
    InvalidTimerID = -1,
    TimerNotFound = -2,
    CallbackEmpty = -3,
    IntervalMustBePositive = -4,
    TimerIdExhausted = -5,
    Unknown = -6,
};

[[nodiscard]] bool IsTimerID(TimerCreateResult value) noexcept;
[[nodiscard]] TimerErrorCode TimerErrorFromCreateResult(TimerCreateResult value) noexcept;
[[nodiscard]] std::string_view TimerErrorMessage(TimerErrorCode error_code) noexcept;

class TimerManager final {
public:
    explicit TimerManager(asio::io_context& io_context);
    ~TimerManager();

    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;
    TimerManager(TimerManager&&) = delete;
    TimerManager& operator=(TimerManager&&) = delete;

    [[nodiscard]] TimerCreateResult CreateOnce(SteadyDuration delay, TimerCallback callback);
    [[nodiscard]] TimerCreateResult CreateRepeating(SteadyDuration interval, TimerCallback callback);
    [[nodiscard]] TimerErrorCode ResetOnce(TimerID timer_id, SteadyDuration delay, TimerCallback callback);
    [[nodiscard]] TimerErrorCode ResetRepeating(TimerID timer_id, SteadyDuration interval, TimerCallback callback);
    [[nodiscard]] TimerErrorCode Cancel(TimerID timer_id) noexcept;
    void CancelAll() noexcept;
    [[nodiscard]] bool Contains(TimerID timer_id) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace xs::core

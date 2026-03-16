#pragma once

#include "TimeUtils.h"

#include <asio/io_context.hpp>

#include <functional>
#include <memory>
#include <string>

namespace xs::core {

using TimerCallback = std::function<void()>;

class SteadyTimer final {
public:
    explicit SteadyTimer(asio::io_context& io_context);
    ~SteadyTimer();

    SteadyTimer(const SteadyTimer&) = delete;
    SteadyTimer& operator=(const SteadyTimer&) = delete;
    SteadyTimer(SteadyTimer&&) = delete;
    SteadyTimer& operator=(SteadyTimer&&) = delete;

    [[nodiscard]] bool StartOnce(
        SteadyDuration delay,
        TimerCallback callback,
        std::string* error_message = nullptr);
    [[nodiscard]] bool StartRepeating(
        SteadyDuration interval,
        TimerCallback callback,
        std::string* error_message = nullptr);

    void Cancel() noexcept;
    [[nodiscard]] bool IsActive() const noexcept;
    [[nodiscard]] bool IsRepeating() const noexcept;
    [[nodiscard]] SteadyDuration interval() const noexcept;

private:
    class State;
    std::shared_ptr<State> state_;
};

} // namespace xs::core

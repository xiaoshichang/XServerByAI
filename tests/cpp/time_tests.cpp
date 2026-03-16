#include "TimeUtils.h"
#include "Timer.h"

#include <asio/io_context.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, const char* message = nullptr) {
    if (condition) {
        return;
    }

    std::cerr << "Check failed: " << expression;
    if (message != nullptr) {
        std::cerr << " (" << message << ")";
    }
    std::cerr << '\n';
    ++g_failures;
}

#define XS_CHECK(expr) Check((expr), #expr)
#define XS_CHECK_MSG(expr, message) Check((expr), #expr, (message))

xs::core::TimerID ExpectTimerCreated(xs::core::TimerCreateResult result) {
    XS_CHECK(xs::core::IsTimerID(result));
    return result;
}

void TestTimeUtilities() {
    XS_CHECK(xs::core::DurationToMilliseconds(std::chrono::seconds(2) + std::chrono::milliseconds(250)) == 2250);
    XS_CHECK(xs::core::ToUnixTimeMilliseconds(xs::core::SystemTimePoint{std::chrono::milliseconds(1234)}) == 1234);

    const auto base_time = xs::core::SteadyTimePoint{};
    const auto deadline = xs::core::SteadyAfter(std::chrono::milliseconds(25), base_time);
    XS_CHECK(deadline == base_time + std::chrono::milliseconds(25));
    XS_CHECK(xs::core::RemainingMilliseconds(deadline, base_time) == std::chrono::milliseconds(25));
    XS_CHECK(xs::core::ClampNonNegativeDuration(-std::chrono::milliseconds(5)) == xs::core::SteadyDuration::zero());
    XS_CHECK(xs::core::RemainingMilliseconds(base_time, deadline) == std::chrono::milliseconds(0));
}

void TestCreateOnceFiresExactlyOnce() {
    asio::io_context io_context;
    xs::core::TimerManager timer_manager(io_context);

    int fire_count = 0;
    const xs::core::TimerID timer_id = ExpectTimerCreated(timer_manager.CreateOnce(std::chrono::milliseconds(1), [&fire_count]() {
        ++fire_count;
    }));

    XS_CHECK(timer_id > 0);
    XS_CHECK(timer_manager.Contains(timer_id));
    XS_CHECK(timer_manager.size() == 1);

    io_context.run();

    XS_CHECK(fire_count == 1);
    XS_CHECK(!timer_manager.Contains(timer_id));
    XS_CHECK(timer_manager.size() == 0);
}

void TestCreateRepeatingCanSelfCancel() {
    asio::io_context io_context;
    xs::core::TimerManager timer_manager(io_context);

    int fire_count = 0;
    xs::core::TimerID timer_id = 0;
    timer_id = ExpectTimerCreated(timer_manager.CreateRepeating(std::chrono::milliseconds(1), [&]() {
        ++fire_count;
        if (fire_count == 3) {
            XS_CHECK(timer_manager.Cancel(timer_id) == xs::core::TimerErrorCode::None);
        }
    }));

    XS_CHECK(timer_manager.Contains(timer_id));
    XS_CHECK(timer_manager.size() == 1);

    io_context.run();

    XS_CHECK(fire_count == 3);
    XS_CHECK(!timer_manager.Contains(timer_id));
    XS_CHECK(timer_manager.size() == 0);
}

void TestCancelSuppressesPendingCallback() {
    asio::io_context io_context;
    xs::core::TimerManager timer_manager(io_context);

    int fire_count = 0;
    const xs::core::TimerID timer_id = ExpectTimerCreated(timer_manager.CreateOnce(std::chrono::milliseconds(20), [&fire_count]() {
        ++fire_count;
    }));

    XS_CHECK(timer_manager.Cancel(timer_id) == xs::core::TimerErrorCode::None);
    io_context.run();

    XS_CHECK(fire_count == 0);
    XS_CHECK(!timer_manager.Contains(timer_id));
}

void TestResetReplacesPendingWait() {
    asio::io_context io_context;
    xs::core::TimerManager timer_manager(io_context);

    int first_callback_count = 0;
    int second_callback_count = 0;
    const xs::core::TimerID timer_id = ExpectTimerCreated(timer_manager.CreateOnce(std::chrono::milliseconds(50), [&first_callback_count]() {
        ++first_callback_count;
    }));

    const auto reset_error = timer_manager.ResetOnce(timer_id, std::chrono::milliseconds(1), [&second_callback_count]() {
        ++second_callback_count;
    });
    XS_CHECK(reset_error == xs::core::TimerErrorCode::None);

    io_context.run();

    XS_CHECK(first_callback_count == 0);
    XS_CHECK(second_callback_count == 1);
    XS_CHECK(!timer_manager.Contains(timer_id));
}

void TestDestroySuppressesPendingCallback() {
    asio::io_context io_context;
    int fire_count = 0;

    {
        auto timer_manager = std::make_unique<xs::core::TimerManager>(io_context);
        const auto timer_id = timer_manager->CreateOnce(std::chrono::milliseconds(20), [&fire_count]() {
            ++fire_count;
        });
        XS_CHECK(xs::core::IsTimerID(timer_id));
    }

    io_context.run();

    XS_CHECK(fire_count == 0);
}

void TestCreateRejectsEmptyCallback() {
    asio::io_context io_context;
    xs::core::TimerManager timer_manager(io_context);

    const auto create_result = timer_manager.CreateOnce(std::chrono::milliseconds(1), xs::core::TimerCallback{});
    XS_CHECK(!xs::core::IsTimerID(create_result));

    const auto error_code = xs::core::TimerErrorFromCreateResult(create_result);
    XS_CHECK(error_code == xs::core::TimerErrorCode::CallbackEmpty);
    XS_CHECK(xs::core::TimerErrorMessage(error_code) == std::string_view("Timer callback must not be empty."));
}

void TestCreateRepeatingRejectsNonPositiveInterval() {
    asio::io_context io_context;
    xs::core::TimerManager timer_manager(io_context);

    const auto create_result = timer_manager.CreateRepeating(std::chrono::milliseconds(0), []() {});
    XS_CHECK(!xs::core::IsTimerID(create_result));

    const auto error_code = xs::core::TimerErrorFromCreateResult(create_result);
    XS_CHECK(error_code == xs::core::TimerErrorCode::IntervalMustBePositive);
    XS_CHECK(xs::core::TimerErrorMessage(error_code) == std::string_view("Repeating timer interval must be greater than zero."));
}

void TestCancelRejectsInvalidOrMissingTimerID() {
    asio::io_context io_context;
    xs::core::TimerManager timer_manager(io_context);

    XS_CHECK(timer_manager.Cancel(0) == xs::core::TimerErrorCode::InvalidTimerID);
    XS_CHECK(timer_manager.Cancel(42) == xs::core::TimerErrorCode::TimerNotFound);
    XS_CHECK(
        xs::core::TimerErrorMessage(xs::core::TimerErrorCode::TimerNotFound) == std::string_view("Timer ID was not found."));
}

void TestResetRejectsInvalidOrMissingTimerID() {
    asio::io_context io_context;
    xs::core::TimerManager timer_manager(io_context);

    XS_CHECK(
        timer_manager.ResetOnce(0, std::chrono::milliseconds(1), []() {}) == xs::core::TimerErrorCode::InvalidTimerID);
    XS_CHECK(
        timer_manager.ResetOnce(42, std::chrono::milliseconds(1), []() {}) == xs::core::TimerErrorCode::TimerNotFound);
}

} // namespace

int main() {
    TestTimeUtilities();
    TestCreateOnceFiresExactlyOnce();
    TestCreateRepeatingCanSelfCancel();
    TestCancelSuppressesPendingCallback();
    TestResetReplacesPendingWait();
    TestDestroySuppressesPendingCallback();
    TestCreateRejectsEmptyCallback();
    TestCreateRepeatingRejectsNonPositiveInterval();
    TestCancelRejectsInvalidOrMissingTimerID();
    TestResetRejectsInvalidOrMissingTimerID();

    if (g_failures != 0) {
        std::cerr << g_failures << " time runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

#include "TimeUtils.h"
#include "Timer.h"

#include <asio/io_context.hpp>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

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

void TestStartOnceFiresExactlyOnce() {
    asio::io_context io_context;
    xs::core::SteadyTimer timer(io_context);

    int fire_count = 0;
    std::string error_message;
    const bool success = timer.StartOnce(std::chrono::milliseconds(1), [&fire_count]() {
        ++fire_count;
    }, &error_message);

    XS_CHECK_MSG(success, error_message.c_str());
    XS_CHECK(timer.IsActive());

    io_context.run();

    XS_CHECK(fire_count == 1);
    XS_CHECK(!timer.IsActive());
    XS_CHECK(!timer.IsRepeating());
}

void TestStartRepeatingCanSelfCancel() {
    asio::io_context io_context;
    xs::core::SteadyTimer timer(io_context);

    int fire_count = 0;
    std::string error_message;
    const bool success = timer.StartRepeating(std::chrono::milliseconds(1), [&]() {
        ++fire_count;
        if (fire_count == 3) {
            timer.Cancel();
        }
    }, &error_message);

    XS_CHECK_MSG(success, error_message.c_str());
    XS_CHECK(timer.IsActive());
    XS_CHECK(timer.IsRepeating());
    XS_CHECK(timer.interval() == std::chrono::milliseconds(1));

    io_context.run();

    XS_CHECK(fire_count == 3);
    XS_CHECK(!timer.IsActive());
}

void TestCancelSuppressesPendingCallback() {
    asio::io_context io_context;
    xs::core::SteadyTimer timer(io_context);

    int fire_count = 0;
    std::string error_message;
    const bool success = timer.StartOnce(std::chrono::milliseconds(20), [&fire_count]() {
        ++fire_count;
    }, &error_message);

    XS_CHECK_MSG(success, error_message.c_str());
    timer.Cancel();
    io_context.run();

    XS_CHECK(fire_count == 0);
    XS_CHECK(!timer.IsActive());
}

void TestRestartReplacesPendingWait() {
    asio::io_context io_context;
    xs::core::SteadyTimer timer(io_context);

    int first_callback_count = 0;
    int second_callback_count = 0;
    std::string error_message;

    const bool first_success = timer.StartOnce(std::chrono::milliseconds(50), [&first_callback_count]() {
        ++first_callback_count;
    }, &error_message);
    XS_CHECK_MSG(first_success, error_message.c_str());

    const bool second_success = timer.StartOnce(std::chrono::milliseconds(1), [&second_callback_count]() {
        ++second_callback_count;
    }, &error_message);
    XS_CHECK_MSG(second_success, error_message.c_str());

    io_context.run();

    XS_CHECK(first_callback_count == 0);
    XS_CHECK(second_callback_count == 1);
}

void TestDestroySuppressesPendingCallback() {
    asio::io_context io_context;
    int fire_count = 0;

    {
        auto timer = std::make_unique<xs::core::SteadyTimer>(io_context);
        std::string error_message;
        const bool success = timer->StartOnce(std::chrono::milliseconds(20), [&fire_count]() {
            ++fire_count;
        }, &error_message);
        XS_CHECK_MSG(success, error_message.c_str());
    }

    io_context.run();

    XS_CHECK(fire_count == 0);
}

void TestRepeatingRejectsNonPositiveInterval() {
    asio::io_context io_context;
    xs::core::SteadyTimer timer(io_context);

    std::string error_message;
    const bool success = timer.StartRepeating(std::chrono::milliseconds(0), []() {}, &error_message);

    XS_CHECK(!success);
    XS_CHECK_MSG(
        error_message.find("greater than zero") != std::string::npos,
        error_message.c_str());
}

} // namespace

int main() {
    TestTimeUtilities();
    TestStartOnceFiresExactlyOnce();
    TestStartRepeatingCanSelfCancel();
    TestCancelSuppressesPendingCallback();
    TestRestartReplacesPendingWait();
    TestDestroySuppressesPendingCallback();
    TestRepeatingRejectsNonPositiveInterval();

    if (g_failures != 0) {
        std::cerr << g_failures << " time runtime test(s) failed.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

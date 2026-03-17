#include "CoreLoopExecutor.h"

#include <asio/executor_work_guard.hpp>

#include <exception>
#include <optional>
#include <string>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace xs::core {
namespace {

void ClearError(std::string* error_message) {
    if (error_message != nullptr) {
        error_message->clear();
    }
}

bool SetError(std::string message, std::string* error_message) {
    if (error_message != nullptr) {
        *error_message = std::move(message);
    }
    return false;
}

#if defined(_WIN32)
std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const int required_size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (required_size <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(required_size), L'\0');
    const int converted_size =
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), wide.data(), required_size);
    if (converted_size != required_size) {
        return {};
    }

    return wide;
}

std::string WideToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }

    const int required_size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required_size <= 1) {
        return {};
    }

    std::string utf8(static_cast<std::size_t>(required_size), '\0');
    const int converted_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value,
        -1,
        utf8.data(),
        required_size,
        nullptr,
        nullptr);
    if (converted_size != required_size) {
        return {};
    }

    utf8.resize(static_cast<std::size_t>(required_size - 1));
    return utf8;
}
#endif

enum class ExecutorState {
    stopped,
    running,
    stopping,
};

} // namespace

class CoreLoopExecutor::Impl final {
public:
    explicit Impl(CoreLoopExecutorOptions options)
        : options_(std::move(options)) {
    }

    bool Start(std::string* error_message) {
        if (state_ != ExecutorState::stopped) {
            return SetError("Core loop executor is already running.", error_message);
        }

        if (options_.thread_name.empty()) {
            return SetError("Core loop executor thread name must not be empty.", error_message);
        }

        io_context_.restart();
        work_guard_.emplace(io_context_.get_executor());
        state_ = ExecutorState::running;

        if (!SetCurrentThreadName(options_.thread_name, error_message)) {
            work_guard_.reset();
            io_context_.stop();
            state_ = ExecutorState::stopped;
            return false;
        }

        try {
            io_context_.run();
        } catch (const std::exception& exception) {
            work_guard_.reset();
            io_context_.stop();
            state_ = ExecutorState::stopped;
            return SetError(std::string("Failed to run core loop executor: ") + exception.what(), error_message);
        }

        work_guard_.reset();
        state_ = ExecutorState::stopped;
        ClearError(error_message);
        return true;
    }

    void Stop() noexcept {
        if (state_ == ExecutorState::stopped) {
            return;
        }

        state_ = ExecutorState::stopping;
        work_guard_.reset();
        io_context_.stop();
    }

    [[nodiscard]] bool IsRunning() const noexcept {
        return state_ != ExecutorState::stopped;
    }

    [[nodiscard]] const CoreLoopExecutorOptions& options() const noexcept {
        return options_;
    }

    [[nodiscard]] asio::any_io_executor executor() noexcept {
        return io_context_.get_executor();
    }

    [[nodiscard]] asio::io_context& context() noexcept {
        return io_context_;
    }

private:
    CoreLoopExecutorOptions options_{};
    asio::io_context io_context_{1};
    std::optional<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_{};
    ExecutorState state_{ExecutorState::stopped};
};

bool SetCurrentThreadName(std::string_view name, std::string* error_message) {
    if (name.empty()) {
        return SetError("Thread name must not be empty.", error_message);
    }

#if defined(_WIN32)
    const auto wide_name = Utf8ToWide(name);
    if (wide_name.empty()) {
        return SetError("Failed to convert thread name to UTF-16.", error_message);
    }

    const HRESULT result = SetThreadDescription(GetCurrentThread(), wide_name.c_str());
    if (FAILED(result)) {
        return SetError("Failed to set current thread name.", error_message);
    }
#elif defined(__APPLE__)
    std::string thread_name{name};
    constexpr std::size_t kMaxLength = 63;
    if (thread_name.size() > kMaxLength) {
        thread_name.resize(kMaxLength);
    }

    if (pthread_setname_np(thread_name.c_str()) != 0) {
        return SetError("Failed to set current thread name.", error_message);
    }
#else
    std::string thread_name{name};
    constexpr std::size_t kMaxLength = 15;
    if (thread_name.size() > kMaxLength) {
        thread_name.resize(kMaxLength);
    }

    if (pthread_setname_np(pthread_self(), thread_name.c_str()) != 0) {
        return SetError("Failed to set current thread name.", error_message);
    }
#endif

    ClearError(error_message);
    return true;
}

std::string CurrentThreadName() {
#if defined(_WIN32)
    PWSTR description = nullptr;
    if (FAILED(GetThreadDescription(GetCurrentThread(), &description)) || description == nullptr) {
        return {};
    }

    std::string thread_name = WideToUtf8(description);
    LocalFree(description);
    return thread_name;
#elif defined(__APPLE__)
    char buffer[64]{};
    if (pthread_getname_np(pthread_self(), buffer, sizeof(buffer)) != 0) {
        return {};
    }

    return std::string(buffer);
#else
    char buffer[16]{};
    if (pthread_getname_np(pthread_self(), buffer, sizeof(buffer)) != 0) {
        return {};
    }

    return std::string(buffer);
#endif
}

CoreLoopExecutor::CoreLoopExecutor(CoreLoopExecutorOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {
}

CoreLoopExecutor::~CoreLoopExecutor() {
    Stop();
}

bool CoreLoopExecutor::Start(std::string* error_message) {
    return impl_->Start(error_message);
}

void CoreLoopExecutor::Stop() noexcept {
    if (impl_ != nullptr) {
        impl_->Stop();
    }
}

bool CoreLoopExecutor::IsRunning() const noexcept {
    return impl_ != nullptr && impl_->IsRunning();
}

const CoreLoopExecutorOptions& CoreLoopExecutor::options() const noexcept {
    return impl_->options();
}

asio::any_io_executor CoreLoopExecutor::executor() noexcept {
    return impl_->executor();
}

asio::io_context& CoreLoopExecutor::context() noexcept {
    return impl_->context();
}

} // namespace xs::core
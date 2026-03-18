#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/io_context.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace xs::core
{

struct CoreLoopExecutorOptions
{
    std::string thread_name{"xs-core-loop"};
};

enum class CoreLoopErrorCode : std::uint8_t
{
    None = 0,
    EmptyThreadName,
    AlreadyRunning,
    ThreadNameConversionFailed,
    ThreadNameSetFailed,
    RunFailed,
    InvalidState,
};

[[nodiscard]] std::string_view CoreLoopErrorMessage(CoreLoopErrorCode code) noexcept;
[[nodiscard]] CoreLoopErrorCode SetCurrentThreadName(std::string_view name, std::string* error_message = nullptr);
[[nodiscard]] std::string CurrentThreadName();

class CoreLoopExecutor final
{
  public:
    explicit CoreLoopExecutor(CoreLoopExecutorOptions options = {});
    ~CoreLoopExecutor();

    CoreLoopExecutor(const CoreLoopExecutor&) = delete;
    CoreLoopExecutor& operator=(const CoreLoopExecutor&) = delete;
    CoreLoopExecutor(CoreLoopExecutor&&) = delete;
    CoreLoopExecutor& operator=(CoreLoopExecutor&&) = delete;

    // Owner-thread only. Runs the io_context on the current thread until Stop() is requested.
    [[nodiscard]] CoreLoopErrorCode Start(std::string* error_message = nullptr);
    // Owner-thread only. Requests the active Start() call on the same thread to exit the event loop.
    void Stop() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] const CoreLoopExecutorOptions& options() const noexcept;
    [[nodiscard]] asio::any_io_executor executor() noexcept;
    [[nodiscard]] asio::io_context& context() noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace xs::core

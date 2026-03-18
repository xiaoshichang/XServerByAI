#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xs::core
{

enum class LogLevel : std::uint8_t
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

enum class ProcessType : std::uint8_t
{
    Gm,
    Gate,
    Game,
};

enum class LoggingErrorCode : std::uint8_t
{
    None = 0,
    EmptyRootDir,
    FlushIntervalMustBePositive,
    MaxFileSizeMustBePositive,
    MaxRetainedFilesMustBePositive,
};

struct LoggingConfig
{
    std::string root_dir{"logs"};
    LogLevel min_level{LogLevel::Info};
    std::uint32_t flush_interval_ms{1000};
    bool rotate_daily{true};
    std::uint32_t max_file_size_mb{64};
    std::uint32_t max_retained_files{10};
};

struct LoggerOptions
{
    ProcessType process_type{ProcessType::Gm};
    std::string instance_id{"GM"};
    LoggingConfig config{};
};

struct LogContextField
{
    std::string key;
    std::string value;
};

class Logger final
{
  public:
    explicit Logger(LoggerOptions options);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    void Log(
        LogLevel level,
        std::string_view category,
        std::string_view message,
        std::span<const LogContextField> context = {},
        std::optional<std::int32_t> error_code = std::nullopt,
        std::string_view error_name = {}) const;

    void Flush() const;
    [[nodiscard]] const LoggerOptions& options() const noexcept;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::optional<LogLevel> ParseLogLevel(std::string_view value) noexcept;
[[nodiscard]] std::string_view LogLevelName(LogLevel value) noexcept;
[[nodiscard]] std::string_view ProcessTypeName(ProcessType value) noexcept;
[[nodiscard]] std::string_view LoggingErrorMessage(LoggingErrorCode code) noexcept;
[[nodiscard]] LoggingErrorCode ValidateLoggingConfig(const LoggingConfig& config, std::string* error_message = nullptr);

} // namespace xs::core

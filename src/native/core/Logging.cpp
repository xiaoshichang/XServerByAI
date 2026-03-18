#include "Logging.h"

#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace xs::core
{
namespace
{

using Clock = std::chrono::system_clock;

struct UtcDate
{
    int year{};
    int month{};
    int day{};

    friend bool operator==(const UtcDate&, const UtcDate&) = default;
};

std::tm ToUtcTm(const Clock::time_point& time_point)
{
    const std::time_t raw_time = Clock::to_time_t(time_point);
    std::tm utc_tm{};

#if defined(_WIN32)
    gmtime_s(&utc_tm, &raw_time);
#else
    gmtime_r(&raw_time, &utc_tm);
#endif

    return utc_tm;
}

UtcDate ToUtcDate(const Clock::time_point& time_point)
{
    const std::tm utc_tm = ToUtcTm(time_point);
    return UtcDate{
        utc_tm.tm_year + 1900,
        utc_tm.tm_mon + 1,
        utc_tm.tm_mday,
    };
}

std::string FormatUtcTimestamp(const Clock::time_point& time_point)
{
    const std::tm utc_tm = ToUtcTm(time_point);
    const auto total_milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(time_point.time_since_epoch()).count();
    const auto milliseconds = static_cast<int>(total_milliseconds % 1000);

    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(4) << utc_tm.tm_year + 1900 << '-'
           << std::setw(2) << utc_tm.tm_mon + 1 << '-'
           << std::setw(2) << utc_tm.tm_mday << 'T'
           << std::setw(2) << utc_tm.tm_hour << ':'
           << std::setw(2) << utc_tm.tm_min << ':'
           << std::setw(2) << utc_tm.tm_sec << '.'
           << std::setw(3) << milliseconds << 'Z';
    return stream.str();
}

std::string FormatDate(const UtcDate& date)
{
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(4) << date.year << '-'
           << std::setw(2) << date.month << '-'
           << std::setw(2) << date.day;
    return stream.str();
}

std::uint32_t GetCurrentProcessIdValue() noexcept
{
#if defined(_WIN32)
    return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint32_t>(::getpid());
#endif
}

spdlog::level::level_enum ToSpdlogLevel(LogLevel value) noexcept
{
    switch (value)
    {
    case LogLevel::Trace:
        return spdlog::level::trace;
    case LogLevel::Debug:
        return spdlog::level::debug;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Warn:
        return spdlog::level::warn;
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Fatal:
        return spdlog::level::critical;
    }

    return spdlog::level::info;
}

bool IsImmediateFlushLevel(LogLevel value) noexcept
{
    return value == LogLevel::Error || value == LogLevel::Fatal;
}

std::string EscapeQuoted(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value)
    {
        switch (ch)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

bool NeedsQuotedContextValue(std::string_view value) noexcept
{
    if (value.empty())
    {
        return true;
    }

    return std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0 || ch == '=' || ch == '"' || ch == '\\';
    });
}

std::string FormatContextValue(std::string_view value)
{
    if (!NeedsQuotedContextValue(value))
    {
        return std::string(value);
    }

    return std::string{"\""} + EscapeQuoted(value) + '"';
}

std::string BuildLogLine(
    ProcessType process_type,
    std::string_view instance_id,
    std::uint32_t process_id,
    LogLevel level,
    std::string_view category,
    std::string_view message,
    std::span<const LogContextField> context,
    std::optional<std::int32_t> error_code,
    std::string_view error_name)
{
    if (instance_id.empty())
    {
        throw std::invalid_argument("Logger instance_id must not be empty.");
    }

    if (category.empty())
    {
        throw std::invalid_argument("Log category must not be empty.");
    }

    if (error_code.has_value() != !error_name.empty())
    {
        throw std::invalid_argument("errorCode and errorName must be provided together.");
    }

    std::string line;
    line.reserve(256);
    line += FormatUtcTimestamp(Clock::now());
    line += ' ';
    line += LogLevelName(level);
    line += ' ';
    line += ProcessTypeName(process_type);
    line += ' ';
    line += instance_id;
    line += " pid=";
    line += std::to_string(process_id);
    line += " cat=";
    line += category;
    line += " msg=\"";
    line += EscapeQuoted(message);
    line += '"';

    if (error_code.has_value())
    {
        line += " errorCode=";
        line += std::to_string(*error_code);
        line += " errorName=";
        line += error_name;
    }

    for (const LogContextField& field : context)
    {
        if (field.key.empty())
        {
            throw std::invalid_argument("Log context key must not be empty.");
        }

        line += ' ';
        line += field.key;
        line += '=';
        line += FormatContextValue(field.value);
    }

    return line;
}

std::string BuildOpenFileError(const std::filesystem::path& path)
{
    std::ostringstream stream;
    stream << "Failed to open log file: " << path.string();
    return stream.str();
}

class DailySizeFileSink final : public spdlog::sinks::base_sink<std::mutex>
{
  public:
    DailySizeFileSink(
        std::filesystem::path root_dir,
        std::string instance_id,
        bool rotate_daily,
        std::uint32_t max_file_size_mb,
        std::uint32_t max_retained_files)
        : root_dir_(std::move(root_dir)),
          instance_id_(std::move(instance_id)),
          rotate_daily_(rotate_daily),
          max_file_size_bytes_(static_cast<std::uintmax_t>(max_file_size_mb) * 1024ULL * 1024ULL),
          max_retained_files_(max_retained_files)
    {
        if (instance_id_.empty())
        {
            throw std::invalid_argument("Logger instance_id must not be empty.");
        }

        if (max_file_size_bytes_ == 0U)
        {
            throw std::invalid_argument("max_file_size_mb must be greater than zero.");
        }
    }

  protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        const std::uintmax_t next_write_size = static_cast<std::uintmax_t>(msg.payload.size()) + 1U;
        EnsureWritableFile(Clock::now(), next_write_size);

        file_.write(msg.payload.data(), static_cast<std::streamsize>(msg.payload.size()));
        file_.put('\n');

        if (!file_)
        {
            throw std::runtime_error(BuildOpenFileError(current_path_));
        }

        current_size_ += next_write_size;
    }

    void flush_() override
    {
        if (file_.is_open())
        {
            file_.flush();
        }
    }

  private:
    struct FileCandidate
    {
        std::size_t suffix{};
        std::filesystem::path path;
        std::uintmax_t size{};
    };

    struct RetainedFile
    {
        std::filesystem::path path;
        std::filesystem::file_time_type last_write_time;
    };

    void EnsureRootDirectory()
    {
        std::error_code error;
        std::filesystem::create_directories(root_dir_, error);
        if (error)
        {
            std::ostringstream stream;
            stream << "Failed to create log directory: " << root_dir_.string() << " (" << error.message() << ')';
            throw std::runtime_error(stream.str());
        }
    }

    [[nodiscard]] std::filesystem::path BuildFilePath(const UtcDate& date, std::size_t suffix) const
    {
        const std::string base_name = instance_id_ + "-" + FormatDate(date);
        if (suffix == 0U)
        {
            return root_dir_ / (base_name + ".log");
        }

        return root_dir_ / (base_name + "." + std::to_string(suffix) + ".log");
    }

    [[nodiscard]] std::optional<std::size_t> TryParseSuffix(
        std::string_view file_name,
        std::string_view base_name) const noexcept
    {
        const std::string primary_name = std::string(base_name) + ".log";
        if (file_name == primary_name)
        {
            return 0U;
        }

        const std::string prefix = std::string(base_name) + ".";
        if (!file_name.starts_with(prefix) || !file_name.ends_with(".log"))
        {
            return std::nullopt;
        }

        const std::string_view digits = file_name.substr(prefix.size(), file_name.size() - prefix.size() - 4U);
        if (digits.empty())
        {
            return std::nullopt;
        }

        std::size_t suffix = 0U;
        for (const char ch : digits)
        {
            if (!std::isdigit(static_cast<unsigned char>(ch)))
            {
                return std::nullopt;
            }

            suffix = (suffix * 10U) + static_cast<std::size_t>(ch - '0');
        }

        return suffix;
    }

    [[nodiscard]] std::vector<FileCandidate> ScanDailyFiles(const UtcDate& date) const
    {
        std::vector<FileCandidate> files;

        std::error_code error;
        if (!std::filesystem::exists(root_dir_, error) || error)
        {
            return files;
        }

        const std::string base_name = instance_id_ + "-" + FormatDate(date);
        std::filesystem::directory_iterator iterator(root_dir_, error);
        std::filesystem::directory_iterator end;
        if (error)
        {
            return files;
        }

        for (; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                break;
            }

            const auto& entry = *iterator;
            if (!entry.is_regular_file(error))
            {
                error.clear();
                continue;
            }

            const std::string file_name = entry.path().filename().string();
            const auto suffix = TryParseSuffix(file_name, base_name);
            if (!suffix.has_value())
            {
                continue;
            }

            std::uintmax_t file_size = 0U;
            file_size = entry.file_size(error);
            if (error)
            {
                error.clear();
                file_size = 0U;
            }

            files.push_back(FileCandidate{
                .suffix = *suffix,
                .path = entry.path(),
                .size = file_size,
            });
        }

        std::sort(files.begin(), files.end(), [](const FileCandidate& lhs, const FileCandidate& rhs) {
            return lhs.suffix < rhs.suffix;
        });
        return files;
    }

    [[nodiscard]] std::pair<std::size_t, std::uintmax_t> SelectWritableFile(
        const UtcDate& date,
        std::uintmax_t next_write_size,
        bool force_new_file) const
    {
        const std::vector<FileCandidate> files = ScanDailyFiles(date);
        if (files.empty())
        {
            return {0U, 0U};
        }

        const FileCandidate& newest = files.back();
        if (force_new_file)
        {
            return {newest.suffix + 1U, 0U};
        }

        if (newest.size + next_write_size <= max_file_size_bytes_)
        {
            return {newest.suffix, newest.size};
        }

        return {newest.suffix + 1U, 0U};
    }

    void CleanupRetainedFiles()
    {
        std::error_code error;
        if (!std::filesystem::exists(root_dir_, error) || error)
        {
            return;
        }

        std::vector<RetainedFile> files;
        const std::string prefix = instance_id_ + "-";
        std::filesystem::directory_iterator iterator(root_dir_, error);
        std::filesystem::directory_iterator end;
        if (error)
        {
            return;
        }

        for (; iterator != end; iterator.increment(error))
        {
            if (error)
            {
                break;
            }

            const auto& entry = *iterator;
            if (!entry.is_regular_file(error))
            {
                error.clear();
                continue;
            }

            const std::string file_name = entry.path().filename().string();
            if (!file_name.starts_with(prefix) || !file_name.ends_with(".log"))
            {
                continue;
            }

            if (entry.path() == current_path_)
            {
                continue;
            }

            const auto last_write_time = entry.last_write_time(error);
            if (error)
            {
                error.clear();
                continue;
            }

            files.push_back(RetainedFile{
                .path = entry.path(),
                .last_write_time = last_write_time,
            });
        }

        if (files.size() + 1U <= max_retained_files_)
        {
            return;
        }

        std::sort(files.begin(), files.end(), [](const RetainedFile& lhs, const RetainedFile& rhs) {
            return std::tie(lhs.last_write_time, lhs.path) < std::tie(rhs.last_write_time, rhs.path);
        });

        const std::size_t removable_count = (files.size() + 1U) - max_retained_files_;
        for (std::size_t index = 0; index < removable_count && index < files.size(); ++index)
        {
            std::filesystem::remove(files[index].path, error);
            error.clear();
        }
    }

    void OpenFile(const UtcDate& date, std::size_t suffix, std::uintmax_t existing_size)
    {
        EnsureRootDirectory();

        if (file_.is_open())
        {
            file_.flush();
            file_.close();
        }

        current_path_ = BuildFilePath(date, suffix);
        file_.open(current_path_, std::ios::binary | std::ios::app);
        if (!file_.is_open())
        {
            throw std::runtime_error(BuildOpenFileError(current_path_));
        }

        current_date_ = date;
        current_suffix_ = suffix;
        current_size_ = existing_size;

        if (current_size_ == 0U)
        {
            std::error_code error;
            const auto file_size = std::filesystem::file_size(current_path_, error);
            if (!error)
            {
                current_size_ = file_size;
            }
        }

        CleanupRetainedFiles();
    }

    void EnsureWritableFile(const Clock::time_point& now, std::uintmax_t next_write_size)
    {
        const UtcDate target_date = (!rotate_daily_ && current_date_.has_value()) ? *current_date_ : ToUtcDate(now);
        const bool has_open_file = file_.is_open();
        const bool date_changed = !current_date_.has_value() || target_date != *current_date_;
        const bool size_exceeded = has_open_file && (current_size_ + next_write_size > max_file_size_bytes_);

        if (!has_open_file || date_changed || size_exceeded)
        {
            const bool force_new_file = has_open_file && !date_changed && size_exceeded;
            const auto [suffix, existing_size] = SelectWritableFile(target_date, next_write_size, force_new_file);
            OpenFile(target_date, suffix, existing_size);
        }
    }

    std::filesystem::path root_dir_;
    std::string instance_id_;
    bool rotate_daily_{};
    std::uintmax_t max_file_size_bytes_{};
    std::uint32_t max_retained_files_{};
    std::ofstream file_;
    std::optional<UtcDate> current_date_;
    std::size_t current_suffix_{};
    std::uintmax_t current_size_{};
    std::filesystem::path current_path_;
};

} // namespace

class Logger::Impl
{
  public:
    explicit Impl(LoggerOptions options)
        : options_(std::move(options)), process_id_(GetCurrentProcessIdValue())
    {
        std::string error_message;
        const LoggingErrorCode validation_result = ValidateLoggingConfig(options_.config, &error_message);
        if (validation_result != LoggingErrorCode::None)
        {
            if (error_message.empty())
            {
                error_message = std::string(LoggingErrorMessage(validation_result));
            }

            throw std::invalid_argument(error_message);
        }

        if (options_.instance_id.empty())
        {
            throw std::invalid_argument("Logger instance_id must not be empty.");
        }

        auto sink = std::make_shared<DailySizeFileSink>(
            options_.config.root_dir,
            options_.instance_id,
            options_.config.rotate_daily,
            options_.config.max_file_size_mb,
            options_.config.max_retained_files);

        logger_ = std::make_shared<spdlog::logger>(options_.instance_id, std::move(sink));
        logger_->set_level(ToSpdlogLevel(options_.config.min_level));
        logger_->flush_on(spdlog::level::err);

        const auto flush_interval = std::chrono::milliseconds(options_.config.flush_interval_ms);
        flush_thread_ = std::jthread([this, flush_interval](std::stop_token stop_token) {
            while (!stop_token.stop_requested())
            {
                std::this_thread::sleep_for(flush_interval);
                if (stop_token.stop_requested())
                {
                    break;
                }

                logger_->flush();
            }
        });
    }

    ~Impl()
    {
        if (flush_thread_.joinable())
        {
            flush_thread_.request_stop();
        }

        if (logger_)
        {
            logger_->flush();
        }
    }

    void Log(
        LogLevel level,
        std::string_view category,
        std::string_view message,
        std::span<const LogContextField> context,
        std::optional<std::int32_t> error_code,
        std::string_view error_name) const
    {
        const std::string line = BuildLogLine(
            options_.process_type,
            options_.instance_id,
            process_id_,
            level,
            category,
            message,
            context,
            error_code,
            error_name);

        logger_->log(ToSpdlogLevel(level), "{}", line);
        if (IsImmediateFlushLevel(level))
        {
            logger_->flush();
        }
    }

    void Flush() const
    {
        logger_->flush();
    }

    [[nodiscard]] const LoggerOptions& options() const noexcept
    {
        return options_;
    }

  private:
    LoggerOptions options_;
    std::uint32_t process_id_{};
    std::shared_ptr<spdlog::logger> logger_;
    std::jthread flush_thread_;
};

Logger::Logger(LoggerOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

Logger::~Logger() = default;

void Logger::Log(
    LogLevel level,
    std::string_view category,
    std::string_view message,
    std::span<const LogContextField> context,
    std::optional<std::int32_t> error_code,
    std::string_view error_name) const
{
    impl_->Log(level, category, message, context, error_code, error_name);
}

void Logger::Flush() const
{
    impl_->Flush();
}

const LoggerOptions& Logger::options() const noexcept
{
    return impl_->options();
}

std::optional<LogLevel> ParseLogLevel(std::string_view value) noexcept
{
    if (value == "Trace")
    {
        return LogLevel::Trace;
    }

    if (value == "Debug")
    {
        return LogLevel::Debug;
    }

    if (value == "Info")
    {
        return LogLevel::Info;
    }

    if (value == "Warn")
    {
        return LogLevel::Warn;
    }

    if (value == "Error")
    {
        return LogLevel::Error;
    }

    if (value == "Fatal")
    {
        return LogLevel::Fatal;
    }

    return std::nullopt;
}

std::string_view LogLevelName(LogLevel value) noexcept
{
    switch (value)
    {
    case LogLevel::Trace:
        return "Trace";
    case LogLevel::Debug:
        return "Debug";
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warn:
        return "Warn";
    case LogLevel::Error:
        return "Error";
    case LogLevel::Fatal:
        return "Fatal";
    }

    return "Info";
}

std::string_view ProcessTypeName(ProcessType value) noexcept
{
    switch (value)
    {
    case ProcessType::Gm:
        return "GM";
    case ProcessType::Gate:
        return "Gate";
    case ProcessType::Game:
        return "Game";
    }

    return "GM";
}

std::string_view LoggingErrorMessage(LoggingErrorCode code) noexcept
{
    switch (code)
    {
    case LoggingErrorCode::None:
        return "No error.";
    case LoggingErrorCode::EmptyRootDir:
        return "logging.rootDir must not be empty.";
    case LoggingErrorCode::FlushIntervalMustBePositive:
        return "logging.flushIntervalMs must be greater than zero.";
    case LoggingErrorCode::MaxFileSizeMustBePositive:
        return "logging.maxFileSizeMB must be greater than zero.";
    case LoggingErrorCode::MaxRetainedFilesMustBePositive:
        return "logging.maxRetainedFiles must be greater than zero.";
    }

    return "Unknown logging error.";
}

LoggingErrorCode ValidateLoggingConfig(const LoggingConfig& config, std::string* error_message)
{
    auto set_error = [error_message](LoggingErrorCode code, std::string_view message) {
        if (error_message != nullptr)
        {
            *error_message = std::string(message);
        }
        return code;
    };

    if (config.root_dir.empty())
    {
        return set_error(LoggingErrorCode::EmptyRootDir, "logging.rootDir must not be empty.");
    }

    if (config.flush_interval_ms == 0U)
    {
        return set_error(
            LoggingErrorCode::FlushIntervalMustBePositive,
            "logging.flushIntervalMs must be greater than zero.");
    }

    if (config.max_file_size_mb == 0U)
    {
        return set_error(
            LoggingErrorCode::MaxFileSizeMustBePositive,
            "logging.maxFileSizeMB must be greater than zero.");
    }

    if (config.max_retained_files == 0U)
    {
        return set_error(
            LoggingErrorCode::MaxRetainedFilesMustBePositive,
            "logging.maxRetainedFiles must be greater than zero.");
    }

    if (error_message != nullptr)
    {
        error_message->clear();
    }

    return LoggingErrorCode::None;
}

} // namespace xs::core

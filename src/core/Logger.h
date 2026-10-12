// Logger.h - thread safe diagnostic logging (FR-09).

#pragma once

#include <array>
#include <cstdarg>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace lspa {

enum class LogLevel {
    Debug = 0,
    Info,
    Warning,
    Error
};

const char* logLevelName(LogLevel lvl);

struct LogEntry {
    LogLevel level;
    std::string timestamp;  // "YYYY-MM-DD HH:MM:SS"
    std::string text;
};

class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel lvl);
    LogLevel level() const;

    // Optional log file; empty path disables file logging.
    bool setFile(const std::string& path);
    std::string filePath() const;

    void log(LogLevel lvl, const std::string& text);
    void logf(LogLevel lvl, const char* fmt, ...);

    // Last N entries for the in-app log viewer.
    std::vector<LogEntry> recent(size_t maxCount = 500) const;

    // Plain text dump (for "copy log" action).
    std::string dump() const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    std::string filePath_;
    void* file_ = nullptr;  // FILE*
    std::deque<LogEntry> ring_;
};

#define LSPA_LOG_DEBUG(...) ::lspa::Logger::instance().logf(::lspa::LogLevel::Debug, __VA_ARGS__)
#define LSPA_LOG_INFO(...)  ::lspa::Logger::instance().logf(::lspa::LogLevel::Info, __VA_ARGS__)
#define LSPA_LOG_WARN(...)  ::lspa::Logger::instance().logf(::lspa::LogLevel::Warning, __VA_ARGS__)
#define LSPA_LOG_ERROR(...) ::lspa::Logger::instance().logf(::lspa::LogLevel::Error, __VA_ARGS__)

}  // namespace lspa

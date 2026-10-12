#include "core/Logger.h"

#include <cstdio>
#include <ctime>

namespace lspa {

const char* logLevelName(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

Logger& Logger::instance() {
    static Logger g;
    return g;
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        std::fclose(static_cast<FILE*>(file_));
        file_ = nullptr;
    }
}

void Logger::setLevel(LogLevel lvl) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = lvl;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

bool Logger::setFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) {
        std::fclose(static_cast<FILE*>(file_));
        file_ = nullptr;
    }
    filePath_.clear();
    if (path.empty()) return true;
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    file_ = f;
    filePath_ = path;
    return true;
}

std::string Logger::filePath() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return filePath_;
}

void Logger::log(LogLevel lvl, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(lvl) < static_cast<int>(level_)) return;

    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm);

    LogEntry e;
    e.level = lvl;
    e.timestamp = stamp;
    e.text = text;
    ring_.push_back(e);
    while (ring_.size() > 1000) ring_.pop_front();

    if (file_) {
        FILE* f = static_cast<FILE*>(file_);
        std::fprintf(f, "%s [%s] %s\n", stamp, logLevelName(lvl), text.c_str());
        std::fflush(f);
    }

    if (lvl == LogLevel::Error || lvl == LogLevel::Warning) {
        std::fprintf(stderr, "%s [%s] %s\n", stamp, logLevelName(lvl), text.c_str());
    }
}

void Logger::logf(LogLevel lvl, const char* fmt, ...) {
    if (static_cast<int>(lvl) < static_cast<int>(level())) return;
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    log(lvl, std::string(buf));
}

std::vector<LogEntry> Logger::recent(size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LogEntry> out;
    size_t n = ring_.size();
    size_t skip = n > maxCount ? n - maxCount : 0;
    out.reserve(n - skip);
    for (size_t i = skip; i < n; ++i) out.push_back(ring_[i]);
    return out;
}

std::string Logger::dump() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string out;
    for (const auto& e : ring_) {
        out += e.timestamp;
        out += " [";
        out += logLevelName(e.level);
        out += "] ";
        out += e.text;
        out += '\n';
    }
    return out;
}

}  // namespace lspa

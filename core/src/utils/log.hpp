#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace driscord {

// None silences all output; ordering Info < Warning < Error < None is intentional.
enum class LogLevel { Info,
    Warning,
    Error,
    None };

inline const char* level_tag(LogLevel l)
{
    switch (l) {
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::None:
        return "";
    }
    return "UNKNOWN";
}

// Minimum level that will actually be printed.  Initialised once from the
// DRISCORD_LOG_LEVEL environment variable (info / warn / error / none).
// Can also be changed at runtime via set_min_log_level().
inline LogLevel& min_log_level()
{
    static LogLevel level = []() -> LogLevel {
        const char* env = std::getenv("DRISCORD_LOG_LEVEL");
        if (!env) {
            return LogLevel::Info;
        }
        std::string_view v(env);
        if (v == "warn" || v == "warning") {
            return LogLevel::Warning;
        }
        if (v == "error") {
            return LogLevel::Error;
        }
        if (v == "none" || v == "silent" || v == "off") {
            return LogLevel::None;
        }
        return LogLevel::Info;
    }();
    return level;
}

inline void set_min_log_level(LogLevel level) { min_log_level() = level; }

class LogMessage {
public:
    explicit LogMessage(LogLevel level)
        : level_(level)
    {
    }

    ~LogMessage()
    {
        if (level_ < min_log_level()) {
            return;
        }

        static std::mutex mtx;
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch())
                            .count()
            % 1000;
        const auto tt = std::chrono::system_clock::to_time_t(now);

        std::tm tm { };
#if defined(_WIN32) && !defined(__MINGW32__)
        // MSVC / native Windows CRT — provides localtime_s
        localtime_s(&tm, &tt);
#else
        // Linux, macOS, and MinGW cross-compile (which has localtime_r via POSIX)
        localtime_r(&tt, &tm);
#endif

        std::scoped_lock lk(mtx);
        auto& out = (level_ == LogLevel::Error) ? std::cerr : std::cout;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
            << std::setw(3) << ms << " [" << level_tag(level_) << "] " << ss_.str()
            << '\n';
    }

    LogMessage(const LogMessage&) = delete;
    LogMessage& operator=(const LogMessage&) = delete;
    LogMessage(LogMessage&&) = default;

    template <typename T>
    LogMessage& operator<<(const T& val)
    {
        ss_ << val;
        return *this;
    }

private:
    LogLevel level_;
    std::ostringstream ss_;
};

} // namespace driscord

#define LOG_INFO() driscord::LogMessage(driscord::LogLevel::Info)
#define LOG_WARNING() driscord::LogMessage(driscord::LogLevel::Warning)
#define LOG_ERROR() driscord::LogMessage(driscord::LogLevel::Error)

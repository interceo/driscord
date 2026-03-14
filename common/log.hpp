#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

namespace driscord {

enum class LogLevel { Info, Warning, Error };

inline const char* level_tag(LogLevel l) {
    switch (l) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "?????";
}

class LogMessage {
public:
    explicit LogMessage(LogLevel level) : level_(level) {}

    ~LogMessage() {
        static std::mutex mtx;
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        auto tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_r(&tt, &tm);

        std::scoped_lock lk(mtx);
        auto& out = (level_ == LogLevel::Error) ? std::cerr : std::cout;
        out << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms << " ["
            << level_tag(level_) << "] " << ss_.str() << '\n';
    }

    LogMessage(const LogMessage&) = delete;
    LogMessage& operator=(const LogMessage&) = delete;
    LogMessage(LogMessage&&) = default;

    template <typename T>
    LogMessage& operator<<(const T& val) {
        ss_ << val;
        return *this;
    }

private:
    LogLevel level_;
    std::ostringstream ss_;
};

}  // namespace driscord

#define LOG_INFO() driscord::LogMessage(driscord::LogLevel::Info)
#define LOG_WARNING() driscord::LogMessage(driscord::LogLevel::Warning)
#define LOG_ERROR() driscord::LogMessage(driscord::LogLevel::Error)

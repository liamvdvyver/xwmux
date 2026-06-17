#pragma once

#include <cstdlib>
#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace xwmux {

enum class LogLevel {
    Error = 0,
    Warn = 1,
    Info = 2,
    Debug = 3,
};

struct LogConfig {
    LogLevel stderr_level = LogLevel::Warn;
    LogLevel notify_level = LogLevel::Error;
};

inline LogConfig &log_config() {
    static LogConfig cfg{};
    return cfg;
}

inline constexpr std::string_view log_level_name(const LogLevel level) {
    switch (level) {
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Debug:
        return "DEBUG";
    }
    std::abort();
}

inline std::string shell_quote(const std::string_view str) {
    std::string quoted{"'"};
    for (char c : str) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(c);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

inline bool should_log_stderr(const LogLevel level) {
    return static_cast<int>(level) <=
           static_cast<int>(log_config().stderr_level);
}

inline bool should_notify(const LogLevel level) {
    return static_cast<int>(level) <=
           static_cast<int>(log_config().notify_level);
}

inline void set_verbosity(const int verbosity) {
    if (verbosity <= 0) {
        log_config().stderr_level = LogLevel::Warn;
        log_config().notify_level = LogLevel::Error;
    } else if (verbosity == 1) {
        log_config().stderr_level = LogLevel::Info;
        log_config().notify_level = LogLevel::Error;
    } else if (verbosity == 2) {
        log_config().stderr_level = LogLevel::Debug;
        log_config().notify_level = LogLevel::Error;
    } else {
        log_config().stderr_level = LogLevel::Debug;
        log_config().notify_level = LogLevel::Debug;
    }
}

inline void log(const LogLevel level, const std::string_view msg) {
    const std::string formatted =
        std::format("[XWMUX][{}] {}", log_level_name(level), msg);

    if (should_log_stderr(level)) {
        std::cerr << formatted;
        if (formatted.empty() || formatted.back() != '\n') {
            std::cerr << '\n';
        }
    }

    if (should_notify(level)) {
        std::string notify_body(msg);
        if (!notify_body.empty() && notify_body.back() == '\n') {
            notify_body.pop_back();
        }
        if (std::system(std::format("notify-send {} {}",
                                    shell_quote(std::string(log_level_name(level))),
                                    shell_quote(notify_body))
                            .c_str())) {
        }
    }
}

inline void log_error(const std::string_view msg) {
    log(LogLevel::Error, msg);
}

inline void log_warn(const std::string_view msg) {
    log(LogLevel::Warn, msg);
}

inline void log_info(const std::string_view msg) {
    log(LogLevel::Info, msg);
}

inline void log_debug(const std::string_view msg) {
    log(LogLevel::Debug, msg);
}

inline void log_msg(const std::string_view msg) {
    log_error(msg);
}

} // namespace xwmux

#pragma once

#include <fmt/format.h>
#include <string>

#define LOG_LEVEL_TRACE 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_WARN 3
#define LOG_LEVEL_ERROR 4
#define LOG_LEVEL_FATAL 5
#define LOG_LEVEL_OFF 6

#define LOG_LEVEL_NAME_TRACE std::string_view("trace", 5)
#define LOG_LEVEL_NAME_DEBUG std::string_view("debug", 5)
#define LOG_LEVEL_NAME_INFO std::string_view("info", 4)
#define LOG_LEVEL_NAME_WARNING std::string_view("warning", 7)
#define LOG_LEVEL_NAME_ERROR std::string_view("error", 5)
#define LOG_LEVEL_NAME_CRITICAL std::string_view("critical", 8)
#define LOG_LEVEL_NAME_OFF std::string_view("off", 3)

#if !defined(LOG_LEVEL_NAMES)
#define LOG_LEVEL_NAMES                                                        \
    {                                                                          \
        LOG_LEVEL_NAME_TRACE, LOG_LEVEL_NAME_DEBUG, LOG_LEVEL_NAME_INFO,       \
        LOG_LEVEL_NAME_WARNING, LOG_LEVEL_NAME_ERROR, LOG_LEVEL_NAME_CRITICAL, \
        LOG_LEVEL_NAME_OFF}
#endif

namespace logger
{
    enum class LogLevel
    {
        Trace = LOG_LEVEL_TRACE,
        Debug = LOG_LEVEL_DEBUG,
        Info = LOG_LEVEL_INFO,
        Warn = LOG_LEVEL_WARN,
        Error = LOG_LEVEL_ERROR,
        Fatal = LOG_LEVEL_FATAL,
        Off = LOG_LEVEL_OFF
    };
    const std::string_view &to_string_view(const LogLevel &l);
    LogLevel from_str(const std::string &name);

    using memory_buf_t = fmt::basic_memory_buffer<char, 250>;

}
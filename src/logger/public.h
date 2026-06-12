#pragma once

#include <fmt/format.h>
#include <string>

#include "utils/null_mutex.h"

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
#define LOG_LEVEL_NAME_WARNING std::string_view("warn", 4)
#define LOG_LEVEL_NAME_ERROR std::string_view("error", 5)
#define LOG_LEVEL_NAME_FATAL std::string_view("fatal", 5)
#define LOG_LEVEL_NAME_OFF std::string_view("off", 3)

#if !defined(LOG_LEVEL_NAMES)
#define LOG_LEVEL_NAMES                                                     \
    {                                                                       \
        LOG_LEVEL_NAME_TRACE, LOG_LEVEL_NAME_DEBUG, LOG_LEVEL_NAME_INFO,    \
        LOG_LEVEL_NAME_WARNING, LOG_LEVEL_NAME_ERROR, LOG_LEVEL_NAME_FATAL, \
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
        Off = LOG_LEVEL_OFF,
        LevelCount
    };
    const std::string_view &to_string_view(const LogLevel &l);
    LogLevel from_str(const std::string &name);

#if defined(LOG_NO_ATOMIC_LEVEL)
    using level_t = utils::null_atomic<LogLevel>;
#else
    using level_t = std::atomic<LogLevel>;
#endif

    using memory_buf_t = fmt::basic_memory_buffer<char, 250>;

    class LogException : public std::exception
    {
    public:
        explicit LogException(std::string msg);
        LogException(std::string msg, int last_errno);
        const char *what() const noexcept override;

    private:
        std::string msg_;
    };

    class FileHelper
    {
    public:
        FileHelper() = default;
        ~FileHelper();

        FileHelper(const FileHelper &) = delete;
        FileHelper &operator=(const FileHelper &) = delete;

        void open(const std::string &fname, bool truncate = false);
        void reopen(bool truncate);
        void flush();
        void sync();
        void close();
        void write(const memory_buf_t &buf);
        size_t size() const;
        const std::string &filename() const;

    private:
        const int open_tries_ = 5;
        const unsigned int open_interval_ = 10;
        std::FILE *fd_{nullptr};
        std::string filename_;
    };
}
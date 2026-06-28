#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <fmt/format.h>

#include "logger/sinks/LogSink.h"

namespace logger
{
    class Logger
    {
    public:
        explicit Logger(std::string name)
            : name_(std::move(name))
        {
        }

        template <typename It>
        Logger(std::string name, It begin, It end)
            : name_(std::move(name)), sinks_(begin, end)
        {
        }

        Logger(std::string name, std::shared_ptr<LogSink> sink)
            : name_(std::move(name)), sinks_({std::move(sink)})
        {
        }

        Logger(std::string name, std::vector<std::shared_ptr<LogSink>> sinks)
            : name_(std::move(name)), sinks_(std::move(sinks))
        {
        }

        virtual ~Logger() = default;
        Logger(const Logger &other);
        Logger(Logger &&other) noexcept;
        Logger &operator=(Logger other) noexcept;
        void swap(Logger &other) noexcept;
        virtual std::shared_ptr<Logger> clone(std::string name);

        bool should_log(LogLevel level) const;

        void set_level(LogLevel level);
        LogLevel get_level() const;
        const std::string &name() const;
        void set_formatter(std::unique_ptr<LogFormatter> formatter);
        void set_pattern(const std::string &pattern);
        void flush();
        void flush_on(LogLevel level);
        LogLevel flush_level() const;

        template <typename... Args>
        void log(LogLevel lvl, fmt::format_string<Args...> fmt, Args &&...args)
        {
            log(details::SourceLocation{}, lvl, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        void log(details::SourceLocation loc, LogLevel lvl, fmt::format_string<Args...> fmt, Args &&...args)
        {
            if (!should_log(lvl))
                return;

            std::string msg = fmt::format(fmt, std::forward<Args>(args)...);
            details::LogEvent event(std::chrono::system_clock::now(), loc, name_, lvl, msg);

            for (auto &sink : sinks_)
            {
                if (sink->should_log(lvl))
                    sink->log(event);
            }

            if (lvl >= flush_level_.load(std::memory_order_relaxed))
                flush();
        }

        template <typename... Args>
        void trace(fmt::format_string<Args...> fmt, Args &&...args)
        {
            log(details::SourceLocation{}, LogLevel::Trace, fmt, std::forward<Args>(args)...);
        }
        template <typename... Args>
        void debug(fmt::format_string<Args...> fmt, Args &&...args)
        {
            log(details::SourceLocation{}, LogLevel::Debug, fmt, std::forward<Args>(args)...);
        }
        template <typename... Args>
        void info(fmt::format_string<Args...> fmt, Args &&...args)
        {
            log(details::SourceLocation{}, LogLevel::Info, fmt, std::forward<Args>(args)...);
        }
        template <typename... Args>
        void warn(fmt::format_string<Args...> fmt, Args &&...args)
        {
            log(details::SourceLocation{}, LogLevel::Warn, fmt, std::forward<Args>(args)...);
        }
        template <typename... Args>
        void error(fmt::format_string<Args...> fmt, Args &&...args)
        {
            log(details::SourceLocation{}, LogLevel::Error, fmt, std::forward<Args>(args)...);
        }
        template <typename... Args>
        void fatal(fmt::format_string<Args...> fmt, Args &&...args)
        {
            log(details::SourceLocation{}, LogLevel::Fatal, fmt, std::forward<Args>(args)...);
        }

    protected:
        std::string name_;
        std::vector<std::shared_ptr<LogSink>> sinks_;
        std::atomic<LogLevel> level_{LogLevel::Info};
        std::atomic<LogLevel> flush_level_{LogLevel::Off};
    };
}

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <fmt/format.h>
#include <functional>
#include <nlohmann/json.hpp>

#include "logger/sinks/LogSink.h"
#include "utils/cached_clock.h"

#define LOGGER_TRY try
#define LOGGER_CATCH(event)                                                     \
    catch (const std::exception &ex)                                            \
    {                                                                           \
        if (event.source.filename)                                              \
        {                                                                       \
            handle_err_(fmt::format(FMT_STRING("{} [{}({})]"), ex.what(),       \
                                    event.source.filename, event.source.line)); \
        }                                                                       \
        else                                                                    \
        {                                                                       \
            handle_err_(ex.what());                                             \
        }                                                                       \
    }                                                                           \
    catch (...)                                                                 \
    {                                                                           \
        handle_err_("Rethrowing unknown exception in logger");                  \
        throw;                                                                  \
    }

namespace logger
{
    class Logger
    {
    public:
        using ptr = std::shared_ptr<Logger>;
        using ErrHandler = std::function<void(const std::string &err_msg)>;
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
        void set_flush_level(LogLevel level);
        LogLevel flush_level() const;
        void set_error_handler(ErrHandler handler);

        virtual nlohmann::json toJson() const;
        virtual std::string toJsonString() const;

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

            // 直接格式化进栈上内联 buffer(250B 内零堆分配),
            // 避免 fmt::format 先落 std::string 再被二次拷贝
            memory_buf_t msg_buf;
            fmt::format_to(std::back_inserter(msg_buf), fmt, std::forward<Args>(args)...);
            // 时间戳走缓存钟(~1ms 粒度): 规避 clock_gettime 在虚拟化环境
            // 下的高开销(VM 拦截实测 ~20us/次), 热路径零系统调用
            details::LogEvent event(utils::cached_wall_now(), loc, name_, lvl,
                                    std::string_view(msg_buf.data(), msg_buf.size()));

            sink_it_(event);
            // for (auto &sink : sinks_)
            // {
            //     if (sink->should_log(lvl))
            //         sink->log(event);
            // }

            // if (lvl >= flush_level_.load(std::memory_order_relaxed))
            //     flush();
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
        virtual void sink_it_(const details::LogEvent &event);
        virtual void flush_();
        void handle_err_(const std::string &msg);

    protected:
        std::string name_;
        std::vector<std::shared_ptr<LogSink>> sinks_;
        std::atomic<LogLevel> level_{LogLevel::Info};
        std::atomic<LogLevel> flush_level_{LogLevel::Off};
        ErrHandler err_handler_{nullptr};
    };
}

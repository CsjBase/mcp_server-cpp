#include "logger/Logger.h"

#include <chrono>
#include <utility>
#include <iostream>
#include <mutex>

namespace logger
{

    Logger::Logger(const Logger &other)
        : name_(other.name_), sinks_(other.sinks_),
          level_(other.level_.load()), flush_level_(other.flush_level_.load()),
          err_handler_(other.err_handler_)
    {
    }

    Logger::Logger(Logger &&other) noexcept
        : name_(std::move(other.name_)), sinks_(std::move(other.sinks_)),
          level_(other.level_.load()), flush_level_(other.flush_level_.load()),
          err_handler_(std::move(other.err_handler_))
    {
    }

    Logger &Logger::operator=(Logger other) noexcept
    {
        this->swap(other);
        return *this;
    }

    void Logger::swap(Logger &other) noexcept
    {
        std::swap(name_, other.name_);
        std::swap(sinks_, other.sinks_);

        auto my_level = level_.load();
        auto other_level = other.level_.load();
        level_.store(other_level);
        other.level_.store(my_level);

        auto my_flush = flush_level_.load();
        auto other_flush = other.flush_level_.load();
        flush_level_.store(other_flush);
        other.flush_level_.store(my_flush);

        std::swap(err_handler_, other.err_handler_);
    }

    std::shared_ptr<Logger> Logger::clone(std::string name)
    {
        auto cloned = std::make_shared<Logger>(*this);
        cloned->name_ = std::move(name);
        return cloned;
    }

    bool Logger::should_log(LogLevel level) const
    {
        return level >= level_.load();
    }

    void Logger::set_level(LogLevel level)
    {
        level_.store(level);
    }

    LogLevel Logger::get_level() const
    {
        return level_.load();
    }

    const std::string &Logger::name() const
    {
        return name_;
    }

    void Logger::set_formatter(std::unique_ptr<LogFormatter> formatter)
    {
        for (auto &sink : sinks_)
        {
            sink->set_formatter(formatter->clone());
        }
    }

    void Logger::set_pattern(const std::string &pattern)
    {
        std::unique_ptr<LogFormatter> formatter(new LogFormatter(pattern));
        for (auto &sink : sinks_)
        {
            sink->set_formatter(formatter->clone());
        }
    }

    void Logger::flush()
    {
        flush_();
    }

    void Logger::set_flush_level(LogLevel level)
    {
        flush_level_.store(level);
    }

    LogLevel Logger::flush_level() const
    {
        return flush_level_.load();
    }

    void Logger::set_error_handler(ErrHandler handler)
    {
        err_handler_ = std::move(handler);
    }

    void Logger::sink_it_(const details::LogEvent &event)
    {
        for (auto &sink : sinks_)
        {
            if (sink->should_log(event.level))
            {
                LOGGER_TRY
                {
                    sink->log(event);
                }
                LOGGER_CATCH(event)
            }
        }
        if (event.level >= flush_level())
            flush_();
    }
    void Logger::flush_()
    {
        for (auto &sink : sinks_)
        {
            try
            {
                sink->flush();
            }
            catch (const std::exception &ex)
            {
                handle_err_(ex.what());
            }
            catch (...)
            {
                handle_err_("Rethrowing unknown exception in logger");
                throw;
            }
        }
    }

    void Logger::handle_err_(const std::string &msg)
    {
        if (err_handler_)
        {
            err_handler_(msg);
        }
        else
        {
            using std::chrono::system_clock;
            static std::mutex mutex;
            static std::chrono::system_clock::time_point last_report_time;
            static size_t err_counter = 0;
            std::lock_guard<std::mutex> lk{mutex};
            auto now = system_clock::now();
            err_counter++;
            if (now - last_report_time < std::chrono::seconds(1))
            {
                return;
            }
            last_report_time = now;
            std::tm tm_time;
            auto time_tt = system_clock::to_time_t(now);
            ::localtime_r(&time_tt, &tm_time);
            char date_buf[64];
            std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", &tm_time);
            std::fprintf(stderr, "[*** LOG ERROR #%04zu ***] [%s] [%s] %s\n", err_counter, date_buf,
                         name().c_str(), msg.c_str());
        }
    }

} // namespace logger

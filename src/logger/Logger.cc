#include "logger/Logger.h"

#include <chrono>
#include <utility>
#include <iostream>

namespace logger
{

    Logger::Logger(const Logger &other)
        : name_(other.name_), sinks_(other.sinks_), level_(other.level_.load()), flush_level_(other.flush_level_.load())
    {
    }

    Logger::Logger(Logger &&other) noexcept
        : name_(std::move(other.name_)), sinks_(std::move(other.sinks_)), level_(other.level_.load()), flush_level_(other.flush_level_.load())
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
        for (auto &sink : sinks_)
        {
            try
            {
                sink->flush();
            }
            catch (const std::exception &e)
            {
                std::cerr << "Exception in sink flush: " << e.what() << std::endl;
            }
            catch (...)
            {
                std::cerr << "Rethrowing unknown exception in logger" << std::endl;
            }
        }
    }

    void Logger::flush_on(LogLevel level)
    {
        flush_level_.store(level);
    }

    LogLevel Logger::flush_level() const
    {
        return flush_level_.load();
    }

} // namespace logger

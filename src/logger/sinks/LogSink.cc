#include "LogSink.h"

namespace logger
{
    void LogSink::set_level(LogLevel level)
    {
        level_.store(level, std::memory_order_relaxed);
    }

    LogLevel LogSink::get_level() const
    {
        return level_.load(std::memory_order_relaxed);
    }

    bool LogSink::should_log(LogLevel level) const
    {
        return level >= level_.load(std::memory_order_relaxed);
    }

}
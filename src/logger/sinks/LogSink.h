#pragma once

#include "logger/public.h"
#include "logger/LogFormatter.h"

#include <string>
#include <nlohmann/json.hpp>

namespace logger
{
    class LogSink
    {
    public:
        virtual ~LogSink() = default;
        virtual void log(const details::LogEvent &event) = 0;
        virtual void flush() = 0;
        virtual void set_pattern(const std::string &pattern) = 0;
        virtual void set_formatter(std::unique_ptr<LogFormatter> formatter) = 0;
        void set_level(LogLevel level);
        LogLevel get_level() const;
        bool should_log(LogLevel level) const;

        virtual nlohmann::json toJson() const = 0;
        virtual std::string toJsonString() const = 0;

    protected:
        level_t level_{LogLevel::Trace};
    };

}

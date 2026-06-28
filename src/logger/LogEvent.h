#pragma once

#include "logger/public.h"

#include <string>
#include <chrono>

namespace logger
{
    namespace details
    {
        struct SourceLocation
        {
            constexpr SourceLocation() = default;
            constexpr SourceLocation(const char *filename_in, int line_in, const char *funcname_in)
                : filename(filename_in), line(line_in), funcname(funcname_in) {}

            constexpr bool empty() const
            {
                return line <= 0;
            }

            const char *filename = nullptr;
            int line = 0;
            const char *funcname = nullptr;
        };

        struct LogEvent
        {
            LogEvent() = default;
            LogEvent(std::chrono::system_clock::time_point log_time,
                     SourceLocation src,
                     std::string_view logger_name,
                     LogLevel lvl,
                     std::string_view msg);
            std::string_view logger_name;
            LogLevel level{LogLevel::Off};
            std::chrono::system_clock::time_point time;
            size_t thread_id{0};
            SourceLocation source;
            std::string_view payload;

            mutable size_t color_range_start{0};
            mutable size_t color_range_end{0};
        };
    }
}
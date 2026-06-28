#pragma once

#include "logger/public.h"
#include "logger/LogEvent.h"

#include <string>
#include <memory>
#include <chrono>
#include <vector>

namespace logger
{
    namespace details
    {
        class LogFormatItem
        {
        public:
            // virtual void format(std::ostream &os, LogFormat &logFormat) = 0;
            virtual ~LogFormatItem() = default;
            virtual void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) = 0;
        };
    }

    class LogFormatter
    {
    public:
        explicit LogFormatter(const std::string &pattern,
                              const std::string &eol = "\n");
        LogFormatter();
        LogFormatter(const LogFormatter &other) = delete;
        LogFormatter &operator=(const LogFormatter &other) = delete;
        std::unique_ptr<LogFormatter> clone() const;
        void format(const details::LogEvent &event, memory_buf_t &dest);
        void set_pattern(std::string pattern);

    private:
        std::string pattern_;
        std::string eol_;
        bool need_time_;
        std::tm cached_tm_;
        std::chrono::seconds last_log_secs_;
        std::vector<std::unique_ptr<details::LogFormatItem>> formatters_;

        void compile_pattern_(const std::string &pattern);
        void handle_flag_(char fmt_c);
    };
}
#include "logger/LogFormatter.h"

#include <string.h>
#include <array>
#include <unistd.h>

namespace logger
{
    namespace details
    {
        //%l 'l'
        class LevelFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                dest.append(logger::to_string_view(event.level));
            }
        };

        //%a 'a'
        static std::array<const char *, 7> week_days{{"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"}};

        class WeekDayFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                dest.append(std::string_view(week_days[tm_time.tm_wday]));
            }
        };

        //%A 'A'
        static std::array<const char *, 7> week_full_days{
            {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"}};

        class WeekFullDayFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                dest.append(std::string_view(week_full_days[tm_time.tm_wday]));
            }
        };

        static const std::array<const char *, 12> months{
            {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"}};
        //%b 'b'
        class MonthFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                dest.append(std::string_view(months[static_cast<size_t>(tm_time.tm_mon)]));
            }
        };

        static const std::array<const char *, 12> full_months{{"January", "February", "March", "April",
                                                               "May", "June", "July", "August", "September",
                                                               "October", "November", "December"}};
        //%B 'B'
        class FullMonthFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                dest.append(std::string_view(full_months[static_cast<size_t>(tm_time.tm_mon)]));
            }
        };

        //%c 'c'
        // Date and time (Fri May 30 00:09:46 2026)
        class DateTimeFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                dest.append(std::string_view(week_days[static_cast<size_t>(tm_time.tm_wday)]));
                dest.push_back(' ');
                dest.append(std::string_view(months[static_cast<size_t>(tm_time.tm_mon)]));
                dest.push_back(' ');
                fmt::format_to(std::back_inserter(dest), "{}", tm_time.tm_mday);
                dest.push_back(' ');
                // time

                fmt::format_to(std::back_inserter(dest), "{:02d}", tm_time.tm_hour);
                dest.push_back(':');
                fmt::format_to(std::back_inserter(dest), "{:02d}", tm_time.tm_min);
                dest.push_back(':');
                fmt::format_to(std::back_inserter(dest), "{:02d}", tm_time.tm_sec);
                dest.push_back(' ');
                fmt::format_to(std::back_inserter(dest), "{}", tm_time.tm_year + 1900);
            }
        };

        //%Y 'Y'
        class YearFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{}", tm_time.tm_year + 1900);
            }
        };

        //%m 'm'
        class MonthNumberFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{:02d}", tm_time.tm_mon + 1);
            }
        };

        //%d 'd'
        class DayFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{:02d}", tm_time.tm_mday);
            }
        };

        //%H 'H'
        class Hours24FormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &tm_time, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{:02d}", tm_time.tm_hour);
            }
        };

        //%I 'I'
        class Hours12FormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &tm_time, memory_buf_t &dest) override
            {
                int h = tm_time.tm_hour % 12;
                if (h == 0)
                    h = 12;
                fmt::format_to(std::back_inserter(dest), "{:02d}", h);
            }
        };

        //%M 'M'
        class MinutesFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &tm_time, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{:02d}", tm_time.tm_min);
            }
        };

        //%S 'S'
        class SecondsFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &tm_time, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{:02d}", tm_time.tm_sec);
            }
        };

        //%e 'e'
        class MillisecondsFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              event.time.time_since_epoch())
                              .count() %
                          1000;
                fmt::format_to(std::back_inserter(dest), "{}", ms);
            }
        };

        //%f 'f'
        class MicrosecondsFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                              event.time.time_since_epoch())
                              .count() %
                          1000000;
                fmt::format_to(std::back_inserter(dest), "{}", us);
            }
        };

        //%F 'F'
        class NanosecondsFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              event.time.time_since_epoch())
                              .count() %
                          1000000000;
                fmt::format_to(std::back_inserter(dest), "{}", ns);
            }
        };

        //%E 'E'
        class EpochSecondsFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                                event.time.time_since_epoch())
                                .count();
                fmt::format_to(std::back_inserter(dest), "{}", secs);
            }
        };

        //%r  'r'   02:55:02 pm
        class Clock12FormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &tm_time, memory_buf_t &dest) override
            {
                // 使用算术运算替代分支来计算12小时制，提高指令流水线效率
                // 公式: (hour + 11) % 12 + 1 能将 0-23 映射为 12, 1-11, 12, 1-11
                int h = (tm_time.tm_hour + 11) % 12 + 1;

                // 确定 am/pm
                const char *ampm = tm_time.tm_hour >= 12 ? "pm" : "am";

                // 格式化输出
                fmt::format_to(std::back_inserter(dest), "{:02d}:{:02d}:{:02d} {}",
                               h, tm_time.tm_min, tm_time.tm_sec, ampm);
            }
        };

        //%R 'R'  24-hour HH:MM
        class Clock24HHMMFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &tm_time, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{:02d}:{:02d}",
                               tm_time.tm_hour, tm_time.tm_min);
            }
        };

        //%T  'T'  ISO 8601 time format (HH:MM:SS)
        class Iso8601TimeFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &tm_time, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{:02d}:{:02d}:{:02d}",
                               tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
            }
        };

        // %z 'z'
        // ±HH:mm
        class UtcOffsetFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &tm_time, memory_buf_t &dest) override
            {
                long offset_secs = tm_time.tm_gmtoff;
                if (offset_secs == 0)
                {
                    dest.push_back('Z');
                }
                else
                {
                    char sign = offset_secs > 0 ? '+' : '-';
                    if (offset_secs < 0)
                        offset_secs = -offset_secs;
                    long offset_h = offset_secs / 3600;
                    long offset_m = (offset_secs % 3600) / 60;
                    fmt::format_to(std::back_inserter(dest), "{}{:02d}:{:02d}",
                                   sign, offset_h, offset_m);
                }
            }
        };

        //%t 't'
        class ThreadIdFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{}", event.thread_id);
            }
        };

        //%P 'P'
        class PidFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &, const std::tm &, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{}", ::getpid());
            }
        };

        //%# '#'
        class SourceLineFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                fmt::format_to(std::back_inserter(dest), "{}", event.line);
            }
        };

        //%s 's'
        class SourceFileFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                dest.append(std::string_view(event.filename));
            }
        };

        //%n 'n'
        class LoggerNameFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                dest.append(event.logger_name);
            }
        };

        //%! '!'
        class FunctionNameFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                dest.append(std::string_view(event.funcname));
            }
        };

        //%v 'v'
        class PayloadFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                dest.append(event.payload);
            }
        };

        // literal char after %
        class CharFormatItem final : public LogFormatItem
        {
        public:
            explicit CharFormatItem(char c) : ch_(c) {}
            void format(const LogEvent &, const std::tm &, memory_buf_t &dest) override
            {
                dest.push_back(ch_);
            }

        private:
            char ch_;
        };

        // %^ '^'
        class ColorStartFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                event.color_range_start = dest.size();
            }
        };

        // %$ '$'
        class ColorEndFormatItem final : public LogFormatItem
        {
        public:
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                event.color_range_end = dest.size();
            }
        };

        // %+ '+'
        // default format: [%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [%s:%#] %v
        class DefaultLogFormatItem final : public LogFormatItem
        {
        public:
            DefaultLogFormatItem()
            {
                // [%Y-%m-%d %H:%M:%S.%e]
                items_.push_back(std::make_unique<CharFormatItem>('['));
                items_.push_back(std::make_unique<YearFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>('-'));
                items_.push_back(std::make_unique<MonthNumberFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>('-'));
                items_.push_back(std::make_unique<DayFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>(' '));
                items_.push_back(std::make_unique<Hours24FormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>(':'));
                items_.push_back(std::make_unique<MinutesFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>(':'));
                items_.push_back(std::make_unique<SecondsFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>('.'));
                items_.push_back(std::make_unique<MillisecondsFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>(']'));
                items_.push_back(std::make_unique<CharFormatItem>(' '));
                // [%n]
                items_.push_back(std::make_unique<CharFormatItem>('['));
                items_.push_back(std::make_unique<LoggerNameFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>(']'));
                items_.push_back(std::make_unique<CharFormatItem>(' '));
                // [%l]
                items_.push_back(std::make_unique<CharFormatItem>('['));
                items_.push_back(std::make_unique<ColorStartFormatItem>());
                items_.push_back(std::make_unique<LevelFormatItem>());
                items_.push_back(std::make_unique<ColorEndFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>(']'));
                items_.push_back(std::make_unique<CharFormatItem>(' '));
                // [%s:%#]
                items_.push_back(std::make_unique<CharFormatItem>('['));
                items_.push_back(std::make_unique<SourceFileFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>(':'));
                items_.push_back(std::make_unique<SourceLineFormatItem>());
                items_.push_back(std::make_unique<CharFormatItem>(']'));
                items_.push_back(std::make_unique<CharFormatItem>(' '));
                // %v
                items_.push_back(std::make_unique<PayloadFormatItem>());
            }
            void format(const LogEvent &event, const std::tm &tm_time, memory_buf_t &dest) override
            {
                for (auto &item : items_)
                    item->format(event, tm_time, dest);
            }

        private:
            std::vector<std::unique_ptr<LogFormatItem>> items_;
        };
        //
        class StringFormatItem final : public LogFormatItem
        {
        public:
            StringFormatItem() = default;
            StringFormatItem(const std::string &str) : str_(str) {}
            void add_ch(char ch) { str_ += ch; }
            bool empty() const { return str_.empty(); }
            void format(const LogEvent &event, const std::tm &, memory_buf_t &dest) override
            {
                dest.append(str_);
            }

        private:
            std::string str_;
        };

    }

    LogFormatter::LogFormatter(const std::string &pattern,
                               const std::string &eol)
        : pattern_(std::move(pattern)),
          eol_(std::move(eol)),
          need_time_(false),
          last_log_secs_(0)
    {
        memset(&cached_tm_, 0, sizeof(cached_tm_));
        compile_pattern_(pattern_);
    }
    LogFormatter::LogFormatter()
        : pattern_("%+"),
          eol_("\n"),
          need_time_(true),
          last_log_secs_(0)
    {
        memset(&cached_tm_, 0, sizeof(cached_tm_));
        compile_pattern_(pattern_);
    }

    void LogFormatter::compile_pattern_(const std::string &pattern)
    {
        formatters_.clear();
        auto cur_str = std::make_unique<details::StringFormatItem>();
        size_t i = 0, n = pattern.size();
        while (i < n)
        {
            char c = pattern[i];
            if (c == '%')
            {
                if (!cur_str->empty())
                    formatters_.push_back(std::move(cur_str));
                cur_str = std::make_unique<details::StringFormatItem>();

                if (i + 1 >= pattern.size())
                {
                    formatters_.push_back(std::make_unique<details::CharFormatItem>('%'));
                    break;
                }
                handle_flag_(pattern[++i]);
                ++i;
            }
            else
            {
                cur_str->add_ch(c);
                ++i;
            }
        }
        if (!cur_str->empty())
            formatters_.push_back(std::move(cur_str));
    }

    void LogFormatter::handle_flag_(char fmt_c)
    {
        switch (fmt_c)
        {
        // default
        case '+':
            formatters_.push_back(std::make_unique<details::DefaultLogFormatItem>());
            goto need_time;
        // level
        case 'l':
            formatters_.push_back(std::make_unique<details::LevelFormatItem>());
            break;
        // abbreviated weekday
        case 'a':
            formatters_.push_back(std::make_unique<details::WeekDayFormatItem>());
            goto need_time;
        // full weekday
        case 'A':
            formatters_.push_back(std::make_unique<details::WeekFullDayFormatItem>());
            goto need_time;
        // abbreviated month
        case 'b':
            formatters_.push_back(std::make_unique<details::MonthFormatItem>());
            goto need_time;
        // full month
        case 'B':
            formatters_.push_back(std::make_unique<details::FullMonthFormatItem>());
            goto need_time;
        // date and time (ctime format)
        case 'c':
            formatters_.push_back(std::make_unique<details::DateTimeFormatItem>());
            goto need_time;
        // year
        case 'Y':
            formatters_.push_back(std::make_unique<details::YearFormatItem>());
            goto need_time;
        // month number 01-12
        case 'm':
            formatters_.push_back(std::make_unique<details::MonthNumberFormatItem>());
            goto need_time;
        // day of month 01-31
        case 'd':
            formatters_.push_back(std::make_unique<details::DayFormatItem>());
            goto need_time;
        // hours 24h 00-23
        case 'H':
            formatters_.push_back(std::make_unique<details::Hours24FormatItem>());
            goto need_time;
        // hours 12h 01-12
        case 'I':
            formatters_.push_back(std::make_unique<details::Hours12FormatItem>());
            goto need_time;
        // minutes 00-59
        case 'M':
            formatters_.push_back(std::make_unique<details::MinutesFormatItem>());
            goto need_time;
        // seconds 00-59
        case 'S':
            formatters_.push_back(std::make_unique<details::SecondsFormatItem>());
            goto need_time;
        // milliseconds
        case 'e':
            formatters_.push_back(std::make_unique<details::MillisecondsFormatItem>());
            break;
        // microseconds
        case 'f':
            formatters_.push_back(std::make_unique<details::MicrosecondsFormatItem>());
            break;
        // nanoseconds
        case 'F':
            formatters_.push_back(std::make_unique<details::NanosecondsFormatItem>());
            break;
        // epoch seconds
        case 'E':
            formatters_.push_back(std::make_unique<details::EpochSecondsFormatItem>());
            break;
        // 12-hour clock 02:55:02 pm
        case 'r':
            formatters_.push_back(std::make_unique<details::Clock12FormatItem>());
            goto need_time;
        // 24-hour HH:MM
        case 'R':
            formatters_.push_back(std::make_unique<details::Clock24HHMMFormatItem>());
            goto need_time;
        // ISO 8601 time HH:MM:SS
        case 'T':
            formatters_.push_back(std::make_unique<details::Iso8601TimeFormatItem>());
            goto need_time;
        // thread id
        case 't':
            formatters_.push_back(std::make_unique<details::ThreadIdFormatItem>());
            break;
        // process id
        case 'P':
            formatters_.push_back(std::make_unique<details::PidFormatItem>());
            break;
        // payload / log message
        case 'v':
            formatters_.push_back(std::make_unique<details::PayloadFormatItem>());
            break;
        // source line number
        case '#':
            formatters_.push_back(std::make_unique<details::SourceLineFormatItem>());
            break;
        // source file name
        case 's':
            formatters_.push_back(std::make_unique<details::SourceFileFormatItem>());
            break;
        // function name
        case '!':
            formatters_.push_back(std::make_unique<details::FunctionNameFormatItem>());
            break;
        // logger name
        case 'n':
            formatters_.push_back(std::make_unique<details::LoggerNameFormatItem>());
            break;
        // UTC offset
        case 'z':
            formatters_.push_back(std::make_unique<details::UtcOffsetFormatItem>());
            break;
        // color start
        case '^':
            formatters_.push_back(std::make_unique<details::ColorStartFormatItem>());
            break;
        // color end
        case '$':
            formatters_.push_back(std::make_unique<details::ColorEndFormatItem>());
            break;
        // literal char
        default:
            formatters_.push_back(std::make_unique<details::CharFormatItem>(fmt_c));
            break;
        }
        return;
    need_time:
        need_time_ = true;
    }

    // std::unique_ptr<LogFormatter> LogFormatter::clone() const
    // {
    //     auto cloned = std::make_unique<LogFormatter>(pattern_, eol_);
    //     cloned->need_time_ = this->need_time_;
    //     return cloned;
    // }
    void LogFormatter::format(const details::LogEvent &event, memory_buf_t &dest)
    {
        if (need_time_)
        {
            auto secs = std::chrono::duration_cast<std::chrono::seconds>(event.time.time_since_epoch());
            if (secs != last_log_secs_)
            {
                auto time = std::chrono::system_clock::to_time_t(event.time);
                localtime_r(&time, &cached_tm_);
                last_log_secs_ = secs;
            }
        }
        for (auto &item : formatters_)
            item->format(event, cached_tm_, dest);

        dest.append(eol_);
    }
    void LogFormatter::set_pattern(std::string pattern)
    {
        pattern_ = std::move(pattern);
        need_time_ = false;
        compile_pattern_(pattern_);
    }
}

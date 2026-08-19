#pragma once

#include "logger/sinks/LogSink.h"
#include "utils/os.h"
#include "logger/sinks/ConsoleMutex.h"
#include "logger/SynchronousFactory.h"

#include <array>

namespace logger
{

    template <typename ConsoleMutex>
    class ColorLogSink : public LogSink
    {
    public:
        using mutex_t = typename ConsoleMutex::mutex_t;
        ColorLogSink(FILE *file);
        ~ColorLogSink() override = default;
        ColorLogSink(const ColorLogSink &other) = delete;
        ColorLogSink(ColorLogSink &&other) = delete;
        ColorLogSink &operator=(const ColorLogSink &other) = delete;
        ColorLogSink &operator=(ColorLogSink &&other) = delete;

        void set_color(LogLevel color_level, std::string_view color);

        void log(const details::LogEvent &msg) override;
        void flush() override;
        void set_pattern(const std::string &pattern) override;
        void set_formatter(std::unique_ptr<LogFormatter> sink_formatter) override;

        // Formatting codes
        const std::string_view reset = "\033[m";
        const std::string_view bold = "\033[1m";
        const std::string_view dark = "\033[2m";
        const std::string_view underline = "\033[4m";
        const std::string_view blink = "\033[5m";
        const std::string_view reverse = "\033[7m";
        const std::string_view concealed = "\033[8m";
        const std::string_view clear_line = "\033[K";

        // Foreground colors
        const std::string_view black = "\033[30m";
        const std::string_view red = "\033[31m";
        const std::string_view green = "\033[32m";
        const std::string_view yellow = "\033[33m";
        const std::string_view blue = "\033[34m";
        const std::string_view magenta = "\033[35m";
        const std::string_view cyan = "\033[36m";
        const std::string_view white = "\033[37m";

        /// Background colors
        const std::string_view on_black = "\033[40m";
        const std::string_view on_red = "\033[41m";
        const std::string_view on_green = "\033[42m";
        const std::string_view on_yellow = "\033[43m";
        const std::string_view on_blue = "\033[44m";
        const std::string_view on_magenta = "\033[45m";
        const std::string_view on_cyan = "\033[46m";
        const std::string_view on_white = "\033[47m";

        /// Bold colors
        const std::string_view yellow_bold = "\033[33m\033[1m";
        const std::string_view red_bold = "\033[31m\033[1m";
        const std::string_view bold_on_red = "\033[1m\033[41m";

    private:
        void print_ccode_(const std::string_view &color_code) const;
        void print_range_(const memory_buf_t &formatted, size_t start, size_t end) const;

    private:
        mutex_t &mutex_;
        FILE *file_;
        std::unique_ptr<LogFormatter> formatter_;
        std::array<std::string, static_cast<size_t>(LogLevel::LevelCount)> colors_;
    };

    template <typename ConsoleMutex>
    class StdoutColorLogSink : public ColorLogSink<ConsoleMutex>
    {
    public:
        StdoutColorLogSink();
    };

    template <typename ConsoleMutex>
    class StderrColorLogSink : public ColorLogSink<ConsoleMutex>
    {
    public:
        StderrColorLogSink();
    };

    template <typename ConsoleMutex>
    ColorLogSink<ConsoleMutex>::ColorLogSink(FILE *file)
        : mutex_(ConsoleMutex::get()),
          file_(file), formatter_(new LogFormatter())
    {
        colors_.at(static_cast<size_t>(LogLevel::Trace)) = white;
        colors_.at(static_cast<size_t>(LogLevel::Debug)) = cyan;
        colors_.at(static_cast<size_t>(LogLevel::Info)) = green;
        colors_.at(static_cast<size_t>(LogLevel::Warn)) = yellow_bold;
        colors_.at(static_cast<size_t>(LogLevel::Error)) = red_bold;
        colors_.at(static_cast<size_t>(LogLevel::Fatal)) = bold_on_red;
        colors_.at(static_cast<size_t>(LogLevel::Off)) = reset;
    }

    template <typename ConsoleMutex>
    void ColorLogSink<ConsoleMutex>::set_color(LogLevel color_level, std::string_view color)
    {
        std::lock_guard<mutex_t> lock(mutex_);
        colors_[static_cast<size_t>(color_level)] = color;
    }

    template <typename ConsoleMutex>
    void ColorLogSink<ConsoleMutex>::log(const details::LogEvent &msg)
    {
        std::lock_guard<mutex_t> lock(mutex_);
        msg.color_range_start = 0;
        msg.color_range_end = 0;
        memory_buf_t formatted;
        formatter_->format(msg, formatted);
        if (msg.color_range_start < msg.color_range_end && formatted.size() >= msg.color_range_end)
        {
            print_range_(formatted, 0, msg.color_range_start);
            print_ccode_(colors_[static_cast<size_t>(msg.level)]);
            print_range_(formatted, msg.color_range_start, msg.color_range_end);
            print_ccode_(reset);
            print_range_(formatted, msg.color_range_end, formatted.size());
        }
        else
        {
            print_range_(formatted, 0, formatted.size());
        }
    }
    template <typename ConsoleMutex>
    void ColorLogSink<ConsoleMutex>::flush()
    {
        std::lock_guard<mutex_t> lock(mutex_);
        fflush(file_);
    }
    template <typename ConsoleMutex>
    void ColorLogSink<ConsoleMutex>::set_pattern(const std::string &pattern)
    {
        std::lock_guard<mutex_t> lock(mutex_);
        formatter_ = std::make_unique<LogFormatter>(pattern);
    }
    template <typename ConsoleMutex>
    void ColorLogSink<ConsoleMutex>::set_formatter(std::unique_ptr<LogFormatter> sink_formatter)
    {
        std::lock_guard<mutex_t> lock(mutex_);
        formatter_ = std::move(sink_formatter);
    }

    template <typename ConsoleMutex>
    void ColorLogSink<ConsoleMutex>::print_ccode_(const std::string_view &color_code) const
    {
        utils::fwrite_bytes(color_code.data(), color_code.size(), file_);
    }
    template <typename ConsoleMutex>
    void ColorLogSink<ConsoleMutex>::print_range_(const memory_buf_t &formatted, size_t start, size_t end) const
    {
        utils::fwrite_bytes(formatted.data() + start, end - start, file_);
    }

    template <typename ConsoleMutex>
    StdoutColorLogSink<ConsoleMutex>::StdoutColorLogSink() : ColorLogSink<ConsoleMutex>(stdout)
    {
    }
    template <typename ConsoleMutex>
    StderrColorLogSink<ConsoleMutex>::StderrColorLogSink() : ColorLogSink<ConsoleMutex>(stderr)
    {
    }

    using StdoutColorLogSinkMT = StdoutColorLogSink<ConsoleMutex>;
    using StdoutColorLogSinkST = StdoutColorLogSink<ConsoleNullMutex>;

    using StderrColorLogSinkMT = StderrColorLogSink<ConsoleMutex>;
    using StderrColorLogSinkST = StderrColorLogSink<ConsoleNullMutex>;

    template <typename Factory = SynchronousFactory>
    inline std::shared_ptr<Logger> create_stdout_color_mt_logger(const std::string &logger_name)
    {
        return Factory::template create<StdoutColorLogSinkMT>(logger_name);
    }

    template <typename Factory = SynchronousFactory>
    inline std::shared_ptr<Logger> create_stdout_color_st_logger(const std::string &logger_name)
    {
        return Factory::template create<StdoutColorLogSinkST>(logger_name);
    }

    template <typename Factory = SynchronousFactory>
    inline std::shared_ptr<Logger> create_stderr_color_mt_logger(const std::string &logger_name)
    {
        return Factory::template create<StderrColorLogSinkMT>(logger_name);
    }

    template <typename Factory = SynchronousFactory>
    inline std::shared_ptr<Logger> create_stderr_color_st_logger(const std::string &logger_name)
    {
        return Factory::template create<StderrColorLogSinkST>(logger_name);
    }

}
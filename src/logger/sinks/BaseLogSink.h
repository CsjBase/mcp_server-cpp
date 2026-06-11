#pragma once

#include "logger/sinks/LogSink.h"
#include "logger/LogFormatter.h"

#include <mutex>

namespace logger
{
    template <typename Mutex>
    class BaseLogSink : public LogSink
    {
    public:
        BaseLogSink(std::unique_ptr<LogFormatter> formatter = std::make_unique<LogFormatter>());
        ~BaseLogSink() = default;

        BaseLogSink(const BaseLogSink &) = delete;
        BaseLogSink(BaseLogSink &&) = delete;

        BaseLogSink &operator=(const BaseLogSink &) = delete;
        BaseLogSink &operator=(BaseLogSink &&) = delete;

        void log(const details::LogEvent &event) override;
        void flush() final override;
        void set_pattern(const std::string &pattern) final override;
        void set_formatter(std::unique_ptr<LogFormatter> sink_formatter) final override;

    protected:
        // sink formatter
        std::unique_ptr<LogFormatter> formatter_;
        Mutex mutex_;

        virtual void sink_it_(const details::LogEvent &msg) = 0;
        virtual void flush_() = 0;
    };

    template <typename Mutex>
    BaseLogSink<Mutex>::BaseLogSink(std::unique_ptr<LogFormatter> formatter)
        : formatter_(std::move(formatter))
    {
    }

    template <typename Mutex>
    void BaseLogSink<Mutex>::log(const details::LogEvent &event)
    {
        std::lock_guard<Mutex> lock(mutex_);
        sink_it_(event);
    }
    template <typename Mutex>
    void BaseLogSink<Mutex>::flush()
    {
        std::lock_guard<Mutex> lock(mutex_);
        flush_();
    }
    template <typename Mutex>
    void BaseLogSink<Mutex>::set_pattern(const std::string &pattern)
    {
        std::lock_guard<Mutex> lock(mutex_);
        formatter_ = std::make_unique<LogFormatter>(pattern);
    }
    template <typename Mutex>
    void BaseLogSink<Mutex>::set_formatter(std::unique_ptr<LogFormatter> sink_formatter)
    {
        std::lock_guard<Mutex> lock(mutex_);
        formatter_ = std::move(sink_formatter);
    }
}
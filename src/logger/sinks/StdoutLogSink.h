#pragma once

#include "logger/sinks/LogSink.h"
#include "utils/os.h"
#include "logger/sinks/ConsoleMutex.h"
#include "logger/SynchronousFactory.h"

namespace logger
{
    template <typename ConsoleMutex>
    class StdoutLogSinkBase : public LogSink
    {
    public:
        using mutex_t = typename ConsoleMutex::mutex_t;
        explicit StdoutLogSinkBase(FILE *file);
        ~StdoutLogSinkBase() override = default;
        void log(const details::LogEvent &event) override;
        void flush() override;
        void set_pattern(const std::string &pattern) override;
        void set_formatter(std::unique_ptr<LogFormatter> formatter) override;

    protected:
        std::unique_ptr<LogFormatter> formatter_;
        mutex_t &mutex_;
        FILE *file_;
    };

    template <typename ConsoleMutex>
    class StdoutLogSink : public StdoutLogSinkBase<ConsoleMutex>
    {
    public:
        StdoutLogSink();
    };

    template <typename ConsoleMutex>
    class StderrLogSink : public StdoutLogSinkBase<ConsoleMutex>
    {
    public:
        StderrLogSink();
    };

    template <typename ConsoleMutex>
    StdoutLogSinkBase<ConsoleMutex>::StdoutLogSinkBase(FILE *file)
        : formatter_(std::make_unique<LogFormatter>()), mutex_(ConsoleMutex::get()), file_(file)
    {
    }

    template <typename ConsoleMutex>
    void StdoutLogSinkBase<ConsoleMutex>::log(const details::LogEvent &event)
    {
        std::lock_guard<mutex_t> lock(mutex_);
        memory_buf_t formatted;
        formatter_->format(event, formatted);
        utils::fwrite_bytes(formatted.data(), formatted.size(), file_);
        ::fflush(file_); // flush every line to terminal
    }

    template <typename ConsoleMutex>
    void StdoutLogSinkBase<ConsoleMutex>::flush()
    {
        std::lock_guard<mutex_t> lock(mutex_);
        utils::fsync(file_);
    }

    template <typename ConsoleMutex>
    void StdoutLogSinkBase<ConsoleMutex>::set_pattern(const std::string &pattern)
    {
        std::lock_guard<mutex_t> lock(mutex_);
        formatter_ = std::make_unique<LogFormatter>(pattern);
    }

    template <typename ConsoleMutex>
    void StdoutLogSinkBase<ConsoleMutex>::set_formatter(std::unique_ptr<LogFormatter> formatter)
    {
        std::lock_guard<mutex_t> lock(mutex_);
        formatter_ = std::move(formatter);
    }

    template <typename ConsoleMutex>
    StdoutLogSink<ConsoleMutex>::StdoutLogSink()
        : StdoutLogSinkBase<ConsoleMutex>(stdout)
    {
    }
    template <typename ConsoleMutex>
    StderrLogSink<ConsoleMutex>::StderrLogSink() : StdoutLogSinkBase<ConsoleMutex>(stderr)
    {
    }

    using StdoutLogSinkMT = StdoutLogSink<ConsoleMutex>;
    using StderrLogSinkMT = StderrLogSink<ConsoleMutex>;

    using StdoutLogSinkST = StdoutLogSink<ConsoleNullMutex>;
    using StderrLogSinkST = StderrLogSink<ConsoleNullMutex>;

    template <typename Factory = SynchronousFactory>
    inline std::shared_ptr<Logger> create_stdout_mt_logger(const std::string &logger_name)
    {
        return Factory::template create<StdoutLogSinkMT>(logger_name);
    }

    template <typename Factory = SynchronousFactory>
    inline std::shared_ptr<Logger> create_stdout_st_logger(const std::string &logger_name)
    {
        return Factory::template create<StdoutLogSinkST>(logger_name);
    }

    template <typename Factory = SynchronousFactory>
    inline std::shared_ptr<Logger> create_stderr_mt_logger(const std::string &logger_name)
    {
        return Factory::template create<StderrLogSinkMT>(logger_name);
    }

    template <typename Factory = SynchronousFactory>
    inline std::shared_ptr<Logger> create_stderr_st_logger(const std::string &logger_name)
    {
        return Factory::template create<StderrLogSinkST>(logger_name);
    }

} // namespace logger

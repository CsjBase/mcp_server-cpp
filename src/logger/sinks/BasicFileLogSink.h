#pragma once

#include "logger/sinks/BaseLogSink.h"

namespace logger
{

    template <typename Mutex>
    class BasicFileLogSink : public BaseLogSink<Mutex>
    {
    public:
        BasicFileLogSink(const std::string &file_path, bool truncate = false);
        const std::string &filename() const;
        void truncate();

    protected:
        void sink_it_(const details::LogEvent &event) override;
        void flush_() override;

    private:
        FileHelper file_helper_;
    };

    template <typename Mutex>
    BasicFileLogSink<Mutex>::BasicFileLogSink(const std::string &file_path, bool truncate)
    {
        file_helper_.open(file_path, truncate);
    }

    template <typename Mutex>
    const std::string &BasicFileLogSink<Mutex>::filename() const
    {
        return file_helper_.filename();
    }

    template <typename Mutex>
    void BasicFileLogSink<Mutex>::truncate()
    {
        std::lock_guard<Mutex> lock(BaseLogSink<Mutex>::mutex_);
        file_helper_.reopen(true);
    }

    template <typename Mutex>
    void BasicFileLogSink<Mutex>::sink_it_(const details::LogEvent &event)
    {
        memory_buf_t formatted;
        BaseLogSink<Mutex>::formatter_->format(event, formatted);
        file_helper_.write(formatted);
    }

    template <typename Mutex>
    void BasicFileLogSink<Mutex>::flush_()
    {
        file_helper_.flush();
    }

    using BasicFileLogSinkMT = BasicFileLogSink<std::mutex>;
    using BasicFileLogSinkST = BasicFileLogSink<utils::null_mutex>;
}
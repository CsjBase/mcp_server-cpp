#pragma once

#include "logger/rotater/RotationStrategy.h"
#include "logger/rotater/RotatedFileHandler.h"
#include "logger/sinks/BaseLogSink.h"
#include "utils/os.h"

#include <fmt/format.h>

namespace logger
{

    template <typename Mutex>
    class RotatingFileLogSink : public BaseLogSink<Mutex>
    {
    public:
        RotatingFileLogSink(std::string base_filename, size_t max_size, size_t max_files,
                            bool rotate_on_open = false,
                            std::unique_ptr<RotatedFileHandler> handler = nullptr);
        std::string filename();
        void rotate_now();
        ~RotatingFileLogSink() = default;

    protected:
        void sink_it_(const details::LogEvent &msg) override;
        void flush_() override;

    private:
        void rotate_();

    private:
        std::string base_filename_;
        size_t max_size_;
        size_t current_size_;
        FileHelper file_helper_;
        std::shared_ptr<RotationStrategy> rotater_;
    };

    template <typename Mutex>
    RotatingFileLogSink<Mutex>::RotatingFileLogSink(std::string base_filename, size_t max_size, size_t max_files,
                                                    bool rotate_on_open,
                                                    std::unique_ptr<RotatedFileHandler> handler)
        : base_filename_(std::move(base_filename)),
          max_size_(max_size),
          current_size_(0),
          rotater_(std::make_shared<SizeBasedRotation>(base_filename_, max_files, std::move(handler)))
    {
        if (max_files == 0)
        {
            throw LogException("rotating sink max_files must be non-zero");
        }

        if (max_size == 0)
        {
            throw LogException("rotating sink max_size must be non-zero");
        }

        file_helper_.open(base_filename_);
        current_size_ = file_helper_.size();
        if (rotate_on_open)
        {
            rotate_();
            current_size_ = 0;
        }
    }

    template <typename Mutex>
    std::string RotatingFileLogSink<Mutex>::filename()
    {
        std::lock_guard<Mutex> lock(BaseLogSink<Mutex>::mutex_);
        return file_helper_.filename();
    }

    template <typename Mutex>
    void RotatingFileLogSink<Mutex>::rotate_now()
    {
        std::lock_guard<Mutex> lock(BaseLogSink<Mutex>::mutex_);
        rotate_();
        current_size_ = 0;
    }

    template <typename Mutex>
    void RotatingFileLogSink<Mutex>::sink_it_(const details::LogEvent &event)
    {
        memory_buf_t formatted;
        BaseLogSink<Mutex>::formatter_->format(event, formatted);
        auto new_size = current_size_ + formatted.size();

        if (new_size >= max_size_)
        {
            file_helper_.flush();
            if (file_helper_.size() > 0)
            {
                rotate_();
                new_size = formatted.size();
            }
        }
        file_helper_.write(formatted);
        current_size_ = new_size;
    }

    template <typename Mutex>
    void RotatingFileLogSink<Mutex>::flush_()
    {
        file_helper_.flush();
    }

    // max_files_ = 3, handler with suffix ".gz.enc":
    // Processed file chain: base_2.log.suffix -> base_3.log.suffix (evicted)
    //                       base_1.log.suffix -> base_2.log.suffix
    // Active file:          base.log          -> base_1.log
    //                       handler->handle("base_1.log") -> base_1.log.suffix
    template <typename Mutex>
    void RotatingFileLogSink<Mutex>::rotate_()
    {
        file_helper_.close();
        try
        {
            rotater_->rotate(base_filename_);
        }
        catch (...)
        {
            file_helper_.reopen(true);
            current_size_ = 0;
            throw;
        }
        file_helper_.reopen(true);
    }

    using RotatingFileLogSinkMT = RotatingFileLogSink<std::mutex>;
    using RotatingFileLogSinkST = RotatingFileLogSink<utils::null_mutex>;
}
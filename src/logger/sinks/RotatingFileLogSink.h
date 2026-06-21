#pragma once

#include "logger/sinks/BaseLogSink.h"
#include "utils/os.h"

#include <fmt/format.h>

namespace logger
{

    template <typename Mutex>
    class RotatingFileLogSink : public BaseLogSink<Mutex>
    {
    public:
        static constexpr size_t kMaxFiles = 200000;
        RotatingFileLogSink(std::string base_filename, size_t max_size, size_t max_files, bool rotate_on_open = false);
        static std::string calc_filename(const std::string &filename, size_t index);
        std::string filename();
        void rotate_now();
        void set_max_size(size_t max_size);
        size_t get_max_size();
        void set_max_files(size_t max_files);
        size_t get_max_files();

    protected:
        void sink_it_(const details::LogEvent &msg) override;
        void flush_() override;

    private:
        void rotate_();
        bool rename_file_(const std::string &src_filename, const std::string &target_filename);

        std::string base_filename_;
        size_t max_size_;
        size_t max_files_;
        size_t current_size_;
        FileHelper file_helper_;
    };

    template <typename Mutex>
    RotatingFileLogSink<Mutex>::RotatingFileLogSink(std::string base_filename, size_t max_size, size_t max_files, bool rotate_on_open)
        : base_filename_(std::move(base_filename)),
          max_size_(max_size),
          max_files_(max_files),
          current_size_(0)
    {
        if (max_files_ == 0)
        {
            throw LogException("rotating sink max_files must be non-zero");
        }

        if (max_size_ == 0)
        {
            throw LogException("rotating sink max_size must be non-zero");
        }

        file_helper_.open(calc_filename(base_filename_, 0));
        current_size_ = file_helper_.size();
        if (rotate_on_open)
        {
            rotate_();
            current_size_ = 0;
        }
    }

    template <typename Mutex>
    std::string RotatingFileLogSink<Mutex>::calc_filename(const std::string &filename, size_t index)
    {
        if (index == 0U)
        {
            return filename;
        }
        auto ext_index = filename.rfind(".");
        if (ext_index == std::string::npos || ext_index == 0 || ext_index == filename.size() - 1)
        {
            return filename;
        }
        auto folder_index = filename.find_last_of("/");
        if (folder_index != std::string::npos && folder_index >= ext_index - 1)
        {
            return filename;
        }
        std::string basename = filename.substr(0, ext_index);
        std::string ext = filename.substr(ext_index);
        return fmt::format("{}_{}{}", basename, index, ext);
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
    void RotatingFileLogSink<Mutex>::set_max_size(size_t max_size)
    {
        std::lock_guard<Mutex> lock(BaseLogSink<Mutex>::mutex_);
        if (max_size == 0)
        {
            throw LogException("rotating sink max_size must be non-zero");
        }
        max_size_ = max_size;
    }

    template <typename Mutex>
    size_t RotatingFileLogSink<Mutex>::get_max_size()
    {
        std::lock_guard<Mutex> lock(BaseLogSink<Mutex>::mutex_);
        return max_size_;
    }

    template <typename Mutex>
    void RotatingFileLogSink<Mutex>::set_max_files(size_t max_files)
    {
        std::lock_guard<Mutex> lock(BaseLogSink<Mutex>::mutex_);
        if (max_files == 0)
        {
            throw LogException("rotating sink max_files must be non-zero");
        }
        max_files_ = max_files;
    }

    template <typename Mutex>
    size_t RotatingFileLogSink<Mutex>::get_max_files()
    {
        std::lock_guard<Mutex> lock(BaseLogSink<Mutex>::mutex_);
        return max_files_;
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

    template <typename Mutex>
    void RotatingFileLogSink<Mutex>::rotate_()
    {
        file_helper_.close();
        for (size_t index = max_files_; index > 0; --index)
        {
            auto src_filename = calc_filename(base_filename_, index - 1);
            if (!utils::path_exists(src_filename))
            {
                continue;
            }
            auto target_filename = calc_filename(base_filename_, index);
            if (!rename_file_(src_filename, target_filename))
            {
                utils::sleep_for_millis(100);
                if (!rename_file_(src_filename, target_filename))
                {
                    file_helper_.reopen(true);
                    current_size_ = 0;
                    throw LogException("rotating_file_sink: failed renaming " + src_filename + " to " + target_filename, errno);
                }
            }
        }
        file_helper_.reopen(true);
    }

    template <typename Mutex>
    bool RotatingFileLogSink<Mutex>::rename_file_(const std::string &src_filename, const std::string &target_filename)
    {
        std::remove(target_filename.c_str());
        return std::rename(src_filename.c_str(), target_filename.c_str()) == 0;
    }

    using RotatingFileLogSinkMT = RotatingFileLogSink<std::mutex>;
    using RotatingFileLogSinkST = RotatingFileLogSink<utils::null_mutex>;
}
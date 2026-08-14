#pragma once

#include "logger/rotater/RotatedFileHandler.h"
#include "logger/sinks/BaseLogSink.h"
#include "utils/os.h"
#include "utils/circular_q.h"
#include "logger/rotater/RotationStrategy.h"

#include <fmt/format.h>

namespace logger
{

    template <typename Mutex>
    class DailyFileLogSink : public BaseLogSink<Mutex>
    {
    public:
        DailyFileLogSink(const std::string &base_filename,
                         int rotation_hour = 0,
                         int rotation_minute = 0,
                         bool truncate = false,
                         uint16_t max_files = 0,
                         std::unique_ptr<RotatedFileHandler> handler = nullptr);

        std::string filename();
        static std::string calc_filename(const std::string &filename, const tm &now_tm);
        static tm now_tm(std::chrono::system_clock::time_point now);

    protected:
        void sink_it_(const details::LogEvent &event) override;
        void flush_() override;

    private:
        std::chrono::system_clock::time_point next_rotation_tp_();

    private:
        std::string base_filename_;
        int rotation_h_;
        int rotation_m_;
        std::chrono::system_clock::time_point rotation_tp_;
        FileHelper file_helper_;
        bool truncate_;
        std::shared_ptr<RotationStrategy> rotater_;
    };

    template <typename Mutex>
    DailyFileLogSink<Mutex>::DailyFileLogSink(const std::string &base_filename,
                                              int rotation_hour,
                                              int rotation_minute,
                                              bool truncate,
                                              uint16_t max_files,
                                              std::unique_ptr<RotatedFileHandler> handler)
        : base_filename_(base_filename),
          rotation_h_(rotation_hour),
          rotation_m_(rotation_minute),
          truncate_(truncate),
          rotater_(std::make_shared<TimeBasedRotation>(base_filename, max_files, std::move(handler)))
    {
        if (rotation_hour > 23 || rotation_hour < 0 || rotation_m_ > 59 || rotation_m_ < 0)
        {
            throw LogException("DailyFileLogSink: Invalid rotation time specified");
        }
        auto now = std::chrono::system_clock::now();
        const auto new_filename = calc_filename(base_filename_, now_tm(now));
        file_helper_.open(new_filename, truncate_);
        rotation_tp_ = next_rotation_tp_();
    }

    template <typename Mutex>
    std::string DailyFileLogSink<Mutex>::filename()
    {
        std::lock_guard<Mutex> lock(BaseLogSink<Mutex>::mutex_);
        return file_helper_.filename();
    }

    template <typename Mutex>
    void DailyFileLogSink<Mutex>::sink_it_(const details::LogEvent &event)
    {
        bool should_rotate = event.time >= rotation_tp_;
        if (should_rotate)
        {
            auto old_filename = file_helper_.filename();
            const auto new_filename = calc_filename(base_filename_, now_tm(event.time));
            file_helper_.open(new_filename, truncate_);
            rotation_tp_ = next_rotation_tp_();
            rotater_->rotate(old_filename);
        }
        memory_buf_t formatted;
        BaseLogSink<Mutex>::formatter_->format(event, formatted);
        file_helper_.write(formatted);
    }

    template <typename Mutex>
    void DailyFileLogSink<Mutex>::flush_()
    {
        file_helper_.flush();
    }

    template <typename Mutex>
    std::string DailyFileLogSink<Mutex>::calc_filename(const std::string &filename, const tm &now_tm)
    {
        std::string basename, ext;
        std::tie(basename, ext) = FileHelper::split_by_extension(filename);

        return fmt::format("{}_{:04d}-{:02d}-{:02d}{}",
                           basename, now_tm.tm_year + 1900, now_tm.tm_mon + 1, now_tm.tm_mday, ext);
    }

    template <typename Mutex>
    tm DailyFileLogSink<Mutex>::now_tm(std::chrono::system_clock::time_point now)
    {
        time_t tnow = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        ::localtime_r(&tnow, &tm);
        return tm;
    }

    template <typename Mutex>
    std::chrono::system_clock::time_point DailyFileLogSink<Mutex>::next_rotation_tp_()
    {
        auto now = std::chrono::system_clock::now();
        tm data = now_tm(now);
        data.tm_hour = rotation_h_;
        data.tm_min = rotation_m_;
        data.tm_sec = 0;
        auto rotation_time = std::chrono::system_clock::from_time_t(std::mktime(&data));
        if (rotation_time > now)
        {
            return rotation_time;
        }
        return {rotation_time + std::chrono::hours(24)};
    }

    using DailyFileLogSinkMT = DailyFileLogSink<std::mutex>;
    using DailyFileLogSinkST = DailyFileLogSink<utils::null_mutex>;
}
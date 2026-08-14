#pragma once

#include <string>
#include <memory>

#include "logger/rotater/RotatedFileHandler.h"
#include "utils/circular_q.h"
#include "utils/thread_pool.h"

namespace logger
{

    /// 日志轮转策略接口。
    /// 负责管理日志文件的重命名链和轮转文件处理逻辑。
    class RotationStrategy
    {
    public:
        RotationStrategy(const std::string &base_filename, size_t max_files, std::unique_ptr<RotatedFileHandler> handler = nullptr)
            : base_filename_(base_filename), max_files_(max_files), handler_(std::move(handler))
        {
        }
        // virtual ~RotationStrategy() = default;
        virtual ~RotationStrategy() = default;

        virtual void rotate(const std::string &rotating_filename) = 0;

        void set_max_files(size_t max_files)
        {
            max_files_ = max_files;
        }

    protected:
        /// @brief  轮转策略专用线程池，只开1个线程串行执行轮转任务防止错乱，只在启用RotatedFileHandler时启用
        static std::unique_ptr<utils::ThreadPool> rotating_thread_pool_;

        std::string base_filename_;
        size_t max_files_ = 0;
        std::unique_ptr<RotatedFileHandler> handler_;
    };

    /// 基于文件大小和索引的轮转策略。
    /// 文件命名规则：base.log, base_1.log, base_2.log, ...
    class SizeBasedRotation final : public RotationStrategy, public std::enable_shared_from_this<SizeBasedRotation>
    {
    public:
        SizeBasedRotation(const std::string &base_filename, size_t max_files, std::unique_ptr<RotatedFileHandler> handler = nullptr)
            : RotationStrategy(base_filename, max_files, std::move(handler))
        {
        }
        virtual void rotate(const std::string &) override;

        ~SizeBasedRotation() override = default;

    protected:
        std::string calc_filename(size_t index);

    private:
        void rename_chain_();

    private:
        std::atomic<size_t> rotate_count_{0};
    };

    /// 基于时间的轮转策略。
    /// 文件命名规则：base_2020-01-01.log, base_2020-01-01.log, base_2020-01-03.log, ...
    class TimeBasedRotation : public RotationStrategy, public std::enable_shared_from_this<TimeBasedRotation>
    {
    public:
        TimeBasedRotation(const std::string &base_filename, size_t max_files, std::unique_ptr<RotatedFileHandler> handler = nullptr)
            : RotationStrategy(base_filename, max_files, std::move(handler)), filenames_q_(max_files)
        {
        }
        virtual void rotate(const std::string &rotating_filename) override;

        ~TimeBasedRotation() override = default;

    private:
        void update_(const std::string &rotating_filename);

    private:
        utils::circular_q<std::string> filenames_q_;
    };

} // namespace logger
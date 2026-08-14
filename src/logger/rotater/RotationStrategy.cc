#include "logger/rotater/RotationStrategy.h"
#include "utils/os.h"
#include "logger/public.h"

namespace logger
{
    std::unique_ptr<utils::ThreadPool> RotationStrategy::rotating_thread_pool_ = std::make_unique<utils::ThreadPool>(1, 256);

    void SizeBasedRotation::rotate(const std::string &)
    {

        // 需要异步任务
        if (handler_)
        {
            // 先把待处理文件重命名到临时文件名
            // base.log -> base.log.proc.rotate_count_
            std::string tmp_path = fmt::format("{}.proc.{}", base_filename_, rotate_count_++);
            if (!utils::rename_file(base_filename_, tmp_path))
            {
                throw LogException(
                    "SizeBasedRotation: failed renaming " + base_filename_ + " to " + tmp_path, errno);
            }

            auto task = [rotater = shared_from_this(), tmp_path]()
            {
                // max_files_ = 3, handler with suffix ".gz.enc":
                // update:      base_2.log.suffix -> base_3.log.suffix (evicted)
                //              base_1.log.suffix -> base_2.log.suffix
                //              base.log.suffix not exists
                rotater->rename_chain_();
                std::string final_path = rotater->calc_filename(1) + rotater->handler_->suffix();

                // handler->handle("base.log.proc.rotate_count_") -> base.log.proc.rotate_count_.suffix
                auto file_path = rotater->handler_->handle(tmp_path);
                // base.log.proc.rotate_count_.suffix -> base_1.log.suffix
                utils::rename_file(file_path, final_path);
            };
            rotating_thread_pool_->submit(task);
        }
        else
        {
            // max_files_ = 3
            // update:      base_2.log -> base_3.log (evicted)
            //              base_1.log -> base_2.log
            // Active file: base.log          -> base_1.log
            rename_chain_();
        }
    }

    std::string SizeBasedRotation::calc_filename(size_t index)
    {
        if (index == 0U)
        {
            return base_filename_;
        }
        std::string basename, ext;
        std::tie(basename, ext) = FileHelper::split_by_extension(base_filename_);
        return fmt::format("{}_{}{}", basename, index, ext);
    }

    void SizeBasedRotation::rename_chain_()
    {
        auto suffix = handler_ ? handler_->suffix() : "";
        for (size_t index = max_files_; index > 0; --index)
        {
            auto src = calc_filename(index - 1) + suffix;
            if (!utils::path_exists(src))
                continue;
            auto dst = calc_filename(index) + suffix;
            if (!utils::rename_file(src, dst))
            {
                throw LogException(
                    "SizeBasedRotation: failed renaming " + src + " to " + dst, errno);
            }
        }
    }

    void TimeBasedRotation::rotate(const std::string &rotating_filename)
    {
        if (handler_)
        {
            auto task = [rotater = shared_from_this(), rotating_filename]()
            {
                auto new_filename = rotater->handler_->handle(rotating_filename);
                rotater->update_(new_filename);
            };
            rotating_thread_pool_->submit(task);
        }
        else
        {
            update_(rotating_filename);
        }
    }

    void TimeBasedRotation::update_(const std::string &rotating_filename)
    {
        if (filenames_q_.full())
        {
            auto filename = filenames_q_.front();
            filenames_q_.pop_front();
            utils::remove_if_exists(filename);
        }
        filenames_q_.push_back(std::string(rotating_filename));
    }

}
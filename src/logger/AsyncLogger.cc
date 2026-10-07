#include "logger/AsyncLogger.h"
#include "logger/LogEventBuffer.h"
#include "utils/cached_clock.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace logger
{
    AsyncLogger::AsyncLogger(
        std::string name, std::shared_ptr<LogSink> sink,
        std::shared_ptr<utils::ThreadPool> tp,
        AsyncOverflowStrategy overflow_strategy,
        size_t batch_size, std::chrono::milliseconds flush_interval)
        : Logger(std::move(name), std::move(sink)), thread_pool_(std::move(tp)),
          overflow_strategy_(overflow_strategy),
          batch_size_(std::max<size_t>(1, batch_size)),
          flush_interval_(flush_interval)
    {
    }

    AsyncLogger::AsyncLogger(
        std::string name, std::vector<std::shared_ptr<LogSink>> sinks,
        std::shared_ptr<utils::ThreadPool> tp,
        AsyncOverflowStrategy overflow_strategy,
        size_t batch_size, std::chrono::milliseconds flush_interval)
        : Logger(std::move(name), std::move(sinks)), thread_pool_(std::move(tp)),
          overflow_strategy_(overflow_strategy),
          batch_size_(std::max<size_t>(1, batch_size)),
          flush_interval_(flush_interval)
    {
    }

    AsyncLogger::~AsyncLogger()
    {
        // 残余批尽力提交。注意: 任务不能捕获 shared_from_this()(析构中
        // weak_ptr 已失效), 故只捕获 sinks 拷贝与批数据, 保持任务自包含;
        // 析构期 Sink 抛错静默忽略。
        std::shared_ptr<std::vector<details::LogEventBuffer>> batch;
        std::vector<std::shared_ptr<LogSink>> sinks_copy;
        {
            std::lock_guard<std::mutex> lock(batch_mutex_);
            batch = takePendingBatch_();
            if (!batch)
                return;
            sinks_copy = sinks_;
        }

        std::shared_ptr<utils::ThreadPool> pool = thread_pool_.lock();
        if (!pool)
        {
            // 线程池已析构(在途批任务持有本对象, 本析构晚于池):
            // 残余批同步写入兜底, 保证不丢日志
            writeBatchToSinksSync_(sinks_copy, *batch);
            for (auto &sink : sinks_copy)
            {
                try
                {
                    sink->flush();
                }
                catch (...)
                {
                }
            }
            return;
        }
        // 注意: batch 必须按值捕获 —— 提交失败时还要用它做同步兜底
        auto task = [sinks = sinks_copy, b = batch]() mutable
        { writeBatchToSinksSync_(sinks, *b); };
        if (!pool->submit(std::move(task)))
        {
            // 线程池已停止: 在析构线程同步写入并 flush, 保证退出不丢残余批
            writeBatchToSinksSync_(sinks_copy, *batch);
            for (auto &sink : sinks_copy)
            {
                try
                {
                    sink->flush();
                }
                catch (...)
                {
                }
            }
        }
    }

    std::shared_ptr<Logger> AsyncLogger::clone(std::string new_name)
    {
        auto tp = thread_pool_.lock();
        if (!tp)
            throw LogException("async logger:thread pool doesn't exist anymore");
        // 批状态不可拷贝(mutex), 显式构造等价状态
        auto cloned = std::make_shared<AsyncLogger>(
            std::move(new_name), sinks_, tp, overflow_strategy_,
            batch_size_, flush_interval_);
        cloned->set_level(level_.load(std::memory_order_relaxed));
        cloned->set_flush_level(flush_level_.load(std::memory_order_relaxed));
        if (err_handler_)
            cloned->set_error_handler(err_handler_);
        return cloned;
    }

    nlohmann::json AsyncLogger::toJson() const
    {
        nlohmann::json json{{"name", name_}, {"logger_type", "async"}};
        for (auto &sink : sinks_)
        {
            json["sinks"].push_back(sink->toJson());
        }
        return json;
    }

    std::string AsyncLogger::toJsonString() const
    {
        return toJson().dump();
    }

    void AsyncLogger::sink_it_(const details::LogEvent &event)
    {
        LOGGER_TRY
        {
            auto pool_ptr = thread_pool_.lock();
            if (!pool_ptr)
            {
                // 线程池已析构: Block 语义下同步写入兜底, 不丢日志
                std::vector<details::LogEventBuffer> single{details::LogEventBuffer(event)};
                writeBatchToSinksSync_(sinks_, single);
                if (event.level >= flush_level())
                    backend_flush_();
                return;
            }

            // 前端攒批: 事件拷入当前批, 仅在批满/时间兜底/紧急级别时投递。
            // 换批与入队在同一临界区内完成, 保证批的入队顺序 == 换出顺序,
            // 从而多生产者场景下同一线程的日志顺序不被打乱。
            std::lock_guard<std::mutex> lock(batch_mutex_);
            if (pending_batch_.empty())
                batch_start_ = utils::cached_steady_now();
            pending_batch_.emplace_back(event);

            const bool batch_full = pending_batch_.size() >= batch_size_;
            const bool time_expired =
                flush_interval_.count() > 0 &&
                utils::cached_steady_now() - batch_start_ >= flush_interval_;
            const bool urgent = event.level >= flush_level();
            if (batch_full || time_expired || urgent)
            {
                auto batch = takePendingBatch_();
                submitBatch_(pool_ptr, std::move(batch));
            }
        }
        LOGGER_CATCH(event)
    }

    void AsyncLogger::flush_()
    {
        LOGGER_TRY
        {
            auto pool_ptr = thread_pool_.lock();
            if (!pool_ptr)
            {
                // 线程池已析构: 残余批与 flush 均同步执行兜底
                std::lock_guard<std::mutex> lock(batch_mutex_);
                auto batch = takePendingBatch_();
                if (batch)
                    writeBatchToSinksSync_(sinks_, *batch);
                backend_flush_();
                return;
            }

            // 残余批 + flush 屏障在同一临界区内入队, 保证顺序:
            // 批先于屏障被处理(FIFO 队列), 且不被其他生产者插队
            auto task = [worker = shared_from_this()]()
            { worker->backend_flush_(); };
            {
                std::lock_guard<std::mutex> lock(batch_mutex_);
                auto batch = takePendingBatch_();
                if (batch)
                    submitBatch_(pool_ptr, std::move(batch));

                if (overflow_strategy_ == AsyncOverflowStrategy::Block)
                {
                    // 提交失败(线程池已停止)时同步 flush 兜底
                    if (!pool_ptr->submit(std::move(task)))
                        backend_flush_();
                }
                else if (overflow_strategy_ == AsyncOverflowStrategy::DiscardNew)
                {
                    pool_ptr->try_submit(std::move(task));
                }
                else
                {
                    pool_ptr->submit_overwrite(std::move(task));
                }
            }
        }
        LOGGER_CATCH(details::LogEvent{})
    }

    void AsyncLogger::backend_sink_it_batch_(
        std::shared_ptr<std::vector<details::LogEventBuffer>> batch)
    {
        bool need_flush = false;
        for (auto &ev : *batch)
        {
            for (auto &sink : sinks_)
            {
                if (sink->should_log(ev.level))
                {
                    LOGGER_TRY { sink->log(ev); }
                    LOGGER_CATCH(ev)
                }
            }
            // 达到 flush 级别: 本批写完后立即落盘(与同步 Logger 语义一致)
            if (ev.level >= flush_level())
                need_flush = true;
        }
        // 已在池线程执行: 直接同步 flush, 不向本池提交 barrier
        // (队列满时 worker 自提交会死锁)
        if (need_flush)
            backend_flush_();
    }

    void AsyncLogger::backend_flush_()
    {
        for (auto &sink : sinks_)
        {
            LOGGER_TRY { sink->flush(); }
            LOGGER_CATCH(details::LogEvent{})
        }
    }

    std::shared_ptr<std::vector<details::LogEventBuffer>> AsyncLogger::takePendingBatch_()
    {
        if (pending_batch_.empty())
            return nullptr;
        auto batch = std::make_shared<std::vector<details::LogEventBuffer>>();
        batch->swap(pending_batch_);
        // 换入的空批立刻预扩容量: 消除增长期 realloc 对已入批元素的
        // 搬迁(每批 9 次 realloc × 250B 内联缓冲, 实测前端成本差 9 倍)
        pending_batch_.reserve(batch_size_);
        return batch;
    }

    void AsyncLogger::submitBatch_(
        const std::shared_ptr<utils::ThreadPool> &pool,
        std::shared_ptr<std::vector<details::LogEventBuffer>> batch)
    {
        auto task = [worker = shared_from_this(), b = batch]() mutable
        { worker->backend_sink_it_batch_(std::move(b)); };

        if (overflow_strategy_ == AsyncOverflowStrategy::Block)
        {
            // Block 语义 = 不丢日志: 线程池停止等提交失败时,
            // 在调用线程同步写入兜底。池停止属收尾阶段, 写完后
            // 直接 flush, 避免内容滞留文件缓冲。
            if (!pool->submit(std::move(task)))
            {
                writeBatchToSinksSync_(sinks_, *batch);
                backend_flush_();
            }
        }
        else if (overflow_strategy_ == AsyncOverflowStrategy::DiscardNew)
        {
            pool->try_submit(std::move(task));
        }
        else
        {
            pool->submit_overwrite(std::move(task));
        }
    }

    void AsyncLogger::writeBatchToSinksSync_(
        const std::vector<std::shared_ptr<LogSink>> &sinks,
        const std::vector<details::LogEventBuffer> &batch)
    {
        for (auto &ev : batch)
        {
            for (auto &sink : sinks)
            {
                if (sink->should_log(ev.level))
                {
                    try
                    {
                        sink->log(ev);
                    }
                    catch (...)
                    {
                        // 兜底路径(析构期/池停止后): 异常静默, 避免二次抛出
                    }
                }
            }
        }
    }

} // namespace logger

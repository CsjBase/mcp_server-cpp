#pragma once

#include "logger/Logger.h"
#include "logger/LogEventBuffer.h"
#include "utils/thread_pool.h"

#include <mutex>
#include <vector>

namespace logger
{
    enum class AsyncOverflowStrategy
    {
        Block,
        DiscardNew,
        DiscardOldest
    };

    /**
     * @brief 异步日志器: 前端攒批 + 后台线程池落盘
     *
     * 前端(log 调用线程)把事件拷入当前批(pending_batch_), 仅在
     * 批满 / 距本批首条超过 flush_interval / 事件级别 >= flush_level
     * 时, 才把整批作为一个任务投递到线程池 —— 把队列互斥与唤醒
     * 系统调用等"投递税"摊薄到每条事件约几纳秒。
     *
     * 溢出策略(AsyncOverflowStrategy)在批提交时生效。
     */
    class AsyncLogger final : public Logger, public std::enable_shared_from_this<AsyncLogger>
    {
    public:
        template <typename It>
        AsyncLogger(std::string name, It begin, It end,
                    std::shared_ptr<utils::ThreadPool> tp,
                    AsyncOverflowStrategy overflow_strategy = AsyncOverflowStrategy::Block,
                    size_t batch_size = 256,
                    std::chrono::milliseconds flush_interval = std::chrono::milliseconds(1))
            : Logger(std::move(name), begin, end),
              thread_pool_(std::move(tp)),
              overflow_strategy_(overflow_strategy),
              batch_size_(batch_size),
              flush_interval_(flush_interval)
        {
        }

        AsyncLogger(std::string name, std::shared_ptr<LogSink> sink,
                    std::shared_ptr<utils::ThreadPool> tp,
                    AsyncOverflowStrategy overflow_strategy = AsyncOverflowStrategy::Block,
                    size_t batch_size = 256,
                    std::chrono::milliseconds flush_interval = std::chrono::milliseconds(1));
        AsyncLogger(std::string name, std::vector<std::shared_ptr<LogSink>> sinks,
                    std::shared_ptr<utils::ThreadPool> tp,
                    AsyncOverflowStrategy overflow_strategy = AsyncOverflowStrategy::Block,
                    size_t batch_size = 256,
                    std::chrono::milliseconds flush_interval = std::chrono::milliseconds(1));

        ~AsyncLogger() override;

        std::shared_ptr<Logger> clone(std::string new_name) override;
        nlohmann::json toJson() const override;
        std::string toJsonString() const override;

        size_t batch_size() const { return batch_size_; }

    protected:
        void sink_it_(const details::LogEvent &) override;
        void flush_() override;

        // ---- 后台线程 ----
        void backend_sink_it_batch_(std::shared_ptr<std::vector<details::LogEventBuffer>> batch);
        void backend_flush_();

        /// 换出当前批(调用方持锁), 空批返回 nullptr
        std::shared_ptr<std::vector<details::LogEventBuffer>> takePendingBatch_();

        /// 按溢出策略把批投递到线程池; Block 策略下提交失败
        /// (线程池已停止)时同步写入兜底, 保证不丢日志
        void submitBatch_(const std::shared_ptr<utils::ThreadPool> &pool,
                          std::shared_ptr<std::vector<details::LogEventBuffer>> batch);

        /// 同步把批写入 sinks(线程池不可用时兜底, 调用线程执行, 异常吞掉)
        static void writeBatchToSinksSync_(
            const std::vector<std::shared_ptr<LogSink>> &sinks,
            const std::vector<details::LogEventBuffer> &batch);

    private:
        std::weak_ptr<utils::ThreadPool> thread_pool_;
        AsyncOverflowStrategy overflow_strategy_{AsyncOverflowStrategy::Block};

        // ---- 攒批状态(仅前端访问, batch_mutex_ 保护) ----
        std::mutex batch_mutex_;
        std::vector<details::LogEventBuffer> pending_batch_;
        std::chrono::steady_clock::time_point batch_start_;
        size_t batch_size_;
        std::chrono::milliseconds flush_interval_;
    };

} // namespace logger

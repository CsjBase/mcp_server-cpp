/**
 * @file async_logger_batch_test.cc
 * @brief 异步日志攒批正确性: 顺序/计数、批满提交、时间兜底、
 *        flush_level 紧急提交、多生产者、析构残余批兜底
 */
#include "logger/AsyncLogger.h"
#include "logger/sinks/LogSink.h"
#include "utils/thread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    using namespace logger;

    /// 记录收到的事件(level + payload), 线程安全
    class CountingSink : public LogSink
    {
    public:
        void log(const details::LogEvent &event) override
        {
            std::lock_guard<std::mutex> lock(mu_);
            entries_.emplace_back(event.level, std::string(event.payload));
        }
        void flush() override {}
        void set_pattern(const std::string &) override {}
        void set_formatter(std::unique_ptr<LogFormatter>) override {}
        nlohmann::json toJson() const override { return {}; }
        std::string toJsonString() const override { return "{}"; }

        size_t count() const
        {
            std::lock_guard<std::mutex> lock(mu_);
            return entries_.size();
        }
        std::vector<std::pair<LogLevel, std::string>> snapshot() const
        {
            std::lock_guard<std::mutex> lock(mu_);
            return entries_;
        }

    private:
        mutable std::mutex mu_;
        std::vector<std::pair<LogLevel, std::string>> entries_;
    };

    /// 屏障等待: FIFO 队列保证屏障执行时此前任务全部完成
    void waitDrain(const std::shared_ptr<utils::ThreadPool> &tp)
    {
        std::promise<void> p;
        auto f = p.get_future();
        tp->submit([&p] { p.set_value(); });
        f.wait();
    }

    bool waitFor(const std::function<bool()> &cond, int timeoutMs = 2000)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (cond())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return cond();
    }

    struct Fixture
    {
        // 单 worker: 批任务串行执行, 事件顺序与提交顺序一致
        // (多 worker 池下批任务并发执行, 不保证跨批全局顺序)
        std::shared_ptr<utils::ThreadPool> tp = std::make_shared<utils::ThreadPool>(1, 65536);
        std::shared_ptr<CountingSink> sink = std::make_shared<CountingSink>();
        std::shared_ptr<AsyncLogger> logger;

        Fixture(size_t batchSize = 256,
                std::chrono::milliseconds flushInterval = std::chrono::milliseconds(1))
            : logger(std::make_shared<AsyncLogger>("batch_test", sink, tp,
                                                   AsyncOverflowStrategy::Block,
                                                   batchSize, flushInterval))
        {
        }
    };

    // 单线程 1 万条: 数量与顺序完全保持
    TEST(AsyncLoggerBatch, OrderAndCountPreserved)
    {
        Fixture f;
        constexpr int kN = 10'000;
        for (int i = 0; i < kN; ++i)
            f.logger->info("msg-{}", i);
        f.logger->flush();
        waitDrain(f.tp);

        ASSERT_EQ(f.sink->count(), (size_t)kN);
        auto entries = f.sink->snapshot();
        for (int i = 0; i < kN; ++i)
            EXPECT_EQ(entries[i].second, "msg-" + std::to_string(i)) << "i=" << i;
    }

    // 批满自动提交: 时间兜底禁用, 整批数的事件无需 flush 即到达
    TEST(AsyncLoggerBatch, FullBatchAutoSubmits)
    {
        Fixture f(/*batchSize=*/16, /*flushInterval=*/std::chrono::hours(1));
        for (int i = 0; i < 100; ++i)
            f.logger->info("full-{}", i);

        // 96 条(6 整批)应已自动提交, 剩余 4 条在残余批中
        EXPECT_TRUE(waitFor([&] { return f.sink->count() >= 96; }));
        f.logger->flush();
        waitDrain(f.tp);
        EXPECT_EQ(f.sink->count(), (size_t)100);
    }

    // 时间兜底: 超过 flush_interval 后, 下一条日志到达时把旧批一并提交。
    // (共享线程池模型下时间检查发生在事件追加时; 完全静默期由
    //  flush()/析构/LoggerManager 周期 flush 兜底)
    TEST(AsyncLoggerBatch, TimeBasedFlushSubmits)
    {
        Fixture f(/*batchSize=*/10'000, /*flushInterval=*/std::chrono::milliseconds(10));
        for (int i = 0; i < 10; ++i)
            f.logger->info("time-{}", i);

        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        f.logger->info("time-trigger"); // 触发过期检查, 连同旧批一起提交

        EXPECT_TRUE(waitFor([&] { return f.sink->count() == 11; }, 3000));
    }

    // flush_level 紧急提交: error 级事件立即触发批提交(语义与同步 Logger 一致)
    TEST(AsyncLoggerBatch, FlushLevelTriggersImmediateSubmit)
    {
        Fixture f(/*batchSize=*/10'000, /*flushInterval=*/std::chrono::hours(1));
        f.logger->set_flush_level(LogLevel::Error);

        for (int i = 0; i < 50; ++i)
            f.logger->info("info-{}", i);
        f.logger->error("boom");

        EXPECT_TRUE(waitFor([&] { return f.sink->count() == 51; }, 3000));
        auto entries = f.sink->snapshot();
        EXPECT_EQ(entries[50].second, "boom");
        EXPECT_EQ(entries[50].first, LogLevel::Error);
    }

    // 多生产者: 总数正确, 每个线程内部顺序保持
    TEST(AsyncLoggerBatch, MultiProducerOrderingPerThread)
    {
        Fixture f(/*batchSize=*/64, /*flushInterval=*/std::chrono::milliseconds(1));
        constexpr int kThreads = 4;
        constexpr int kPerThread = 1'000;

        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t)
        {
            ts.emplace_back([&, t]
                            {
                                for (int i = 0; i < kPerThread; ++i)
                                    f.logger->info("t{}-{}", t, i);
                            });
        }
        for (auto &t : ts)
            t.join();
        f.logger->flush();
        waitDrain(f.tp);

        ASSERT_EQ(f.sink->count(), (size_t)kThreads * kPerThread);
        auto entries = f.sink->snapshot();

        // 按线程分组验证各自 seq 严格递增
        std::vector<int> nextSeq(kThreads, 0);
        for (auto &[lvl, payload] : entries)
        {
            int t = payload[1] - '0'; // "t{0}-123"
            size_t dash = payload.find('-');
            int seq = std::stoi(payload.substr(dash + 1));
            EXPECT_EQ(seq, nextSeq[t]) << "thread " << t;
            ++nextSeq[t];
        }
        for (int t = 0; t < kThreads; ++t)
            EXPECT_EQ(nextSeq[t], kPerThread);
    }

    // 析构兜底: 残余批在析构时仍被提交, 不丢事件
    TEST(AsyncLoggerBatch, DestructorFlushesPendingBatch)
    {
        auto tp = std::make_shared<utils::ThreadPool>(1, 1024);
        auto sink = std::make_shared<CountingSink>();
        {
            auto logger = std::make_shared<AsyncLogger>(
                "dtor_test", sink, tp, AsyncOverflowStrategy::Block,
                /*batchSize=*/10'000, /*flushInterval=*/std::chrono::hours(1));
            for (int i = 0; i < 5; ++i)
                logger->info("dtor-{}", i);
            // 离开作用域 → 析构提交残余批
        }
        waitDrain(tp);
        EXPECT_EQ(sink->count(), (size_t)5);
    }

} // namespace

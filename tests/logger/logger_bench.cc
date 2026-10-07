/**
 * @file logger_bench.cc
 * @brief 日志吞吐基准: 同步 vs 异步(各配 NullSink / FileSink)
 *
 * NullSink(丢弃输出) 测量 Logger 前端纯 CPU 成本:
 * 格式化(fmt) + 级别过滤 + 分发/投递;
 * FileSink 测量实际落盘吞吐。
 * 异步场景: 每条日志打包为 LogEventBuffer 投递到 utils::ThreadPool,
 * 前端与落盘解耦 —— 关注"业务线程写日志的开销"与"总吞吐"两个视角。
 * 每场景预热 1 轮 + 3 轮取最优。
 */
#include "logger/Logger.h"
#include "logger/AsyncLogger.h"
#include "logger/sinks/BasicFileLogSink.h"
#include "logger/sinks/LogSink.h"
#include "utils/thread_pool.h"

#include <chrono>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace
{
    constexpr uint64_t kIters = 1'000'000;
    constexpr uint64_t kWarmup = 100'000;
    constexpr int kRounds = 3;

    struct Result
    {
        double opsPerSec = 0;
        double nsPerOp = 0;
    };

    /// 丢弃输出的 Sink: 隔离测量 Logger 前端成本
    class NullLogSink : public logger::LogSink
    {
    public:
        void log(const logger::details::LogEvent &) override {}
        void flush() override {}
        void set_pattern(const std::string &) override {}
        void set_formatter(std::unique_ptr<logger::LogFormatter>) override {}
        nlohmann::json toJson() const override { return {}; }
        std::string toJsonString() const override { return "{}"; }
    };

    inline double nowSec()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void runLoop(const std::shared_ptr<logger::Logger> &l, uint64_t iters)
    {
        for (uint64_t i = 0; i < iters; ++i)
            l->info("bench message {} with {} args", i, 3.14);
    }

    /// 屏障等待: FIFO 队列保证屏障任务执行时, 之前的日志任务已全部处理完
    void waitDrain(const std::shared_ptr<utils::ThreadPool> &tp)
    {
        std::promise<void> p;
        auto f = p.get_future();
        tp->submit([&p]
                   { p.set_value(); });
        f.wait();
    }

    Result benchSync(const std::shared_ptr<logger::LogSink> &sink)
    {
        auto l = std::make_shared<logger::Logger>("bench_sync", sink);
        runLoop(l, kWarmup);

        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            double t0 = nowSec();
            runLoop(l, kIters);
            double sec = nowSec() - t0;

            double ops = (double)kIters / sec;
            if (ops > best.opsPerSec)
            {
                best.opsPerSec = ops;
                best.nsPerOp = 1e9 / ops;
            }
        }
        return best;
    }

    Result benchAsync(const std::shared_ptr<logger::LogSink> &sink, size_t poolThreads)
    {
        auto tp = std::make_shared<utils::ThreadPool>(poolThreads, 65536);
        auto l = std::make_shared<logger::AsyncLogger>("bench_async", sink, tp);
        runLoop(l, kWarmup);
        waitDrain(tp);

        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            double t0 = nowSec();
            runLoop(l, kIters);
            waitDrain(tp);
            double sec = nowSec() - t0;

            double ops = (double)kIters / sec;
            if (ops > best.opsPerSec)
            {
                best.opsPerSec = ops;
                best.nsPerOp = 1e9 / ops;
            }
        }
        // tp->stop_gracefully();
        return best;
    }

    void report(const char *scenario, const Result &r)
    {
        std::cout << std::left << std::setw(30) << scenario
                  << std::right << std::setw(14) << (long long)r.opsPerSec << " ops/s"
                  << std::setw(10) << (long long)r.nsPerOp << " ns/op\n";
    }

} // namespace

int main()
{
    auto nullSink = std::make_shared<NullLogSink>();
    auto fileSink = std::make_shared<logger::BasicFileLogSinkMT>("/tmp/logger_bench.log", /*truncate=*/true);

    std::cout << "每场景日志条数: " << kIters << ", 预热: " << kWarmup
              << ", 轮数(取最优): " << kRounds << "\n\n";
    std::cout << std::left << std::setw(30) << "场景"
              << std::right << std::setw(14) << "吞吐"
              << std::setw(14) << "单条耗时\n";
    std::cout << std::string(58, '-') << "\n";

    report("同步 + NullSink", benchSync(nullSink));
    report("同步 + FileSink(落盘)", benchSync(fileSink));
    report("异步 + NullSink(1后台线程)", benchAsync(nullSink, 1));
    report("异步 + NullSink(4后台线程)", benchAsync(nullSink, 4));
    report("异步 + FileSink(1后台线程)", benchAsync(fileSink, 1));
    return 0;
}

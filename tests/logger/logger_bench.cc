/**
 * @file logger_bench.cc
 * @brief 日志吞吐基准: 同步 vs 异步(各配 NullSink / FileSink / 轮转+处理链)
 *
 * NullSink(丢弃输出) 测量 Logger 前端纯 CPU 成本:
 * 格式化(fmt) + 级别过滤 + 分发/投递;
 * FileSink 测量实际落盘吞吐;
 * RotatingFileSink + 压缩/加密链测量轮转场景: 轮转(rename+reopen)
 * 发生在写路径内, 压缩/加密在全局轮转线程池异步执行;
 * 端到端场景把轮转池排空计入计时窗口, 测量含压缩链的可持续
 * 吞吐(压缩速率跟不上时背压传导至写路径);
 * 多线程场景测量 MT Sink 互斥与异步前端攒批锁的争用表现。
 * 异步场景: 每条日志打包为 LogEventBuffer 投递到 utils::ThreadPool,
 * 线程池统一单线程 + 8192 队列, 前端与落盘解耦。
 * 每场景预热 1 轮 + 3 轮取最优。
 */
#include "logger/Logger.h"
#include "logger/AsyncLogger.h"
#include "logger/sinks/BasicFileLogSink.h"
#include "logger/sinks/LogSink.h"
#include "logger/sinks/RotatingFileLogSink.h"
#include "logger/rotater/RotatedFileHandler.h"
#include "logger/rotater/CompressHandler.h"
#include "logger/rotater/EncryptHandler.h"
#include "utils/thread_pool.h"

#include <chrono>
#include <cstdio>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <sys/statfs.h>

namespace
{
    constexpr uint64_t kIters = 1'000'000;
    constexpr uint64_t kWarmup = 100'000;
    constexpr int kRounds = 3;
    constexpr int kMtThreads = 4;
    constexpr uint64_t kMtItersPerThread = 250'000;
    constexpr uint64_t kMtWarmupPerThread = 25'000;
    constexpr size_t kRotateSize = 1 << 20; // 1MB 触发轮转
    constexpr size_t kRotateMaxFiles = 3;
    constexpr size_t kAsyncQueueSize = 8192;

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

    /// 屏障等待: FIFO 队列保证屏障任务执行时, 之前的任务已全部处理完
    void waitDrain(utils::ThreadPool *tp)
    {
        std::promise<void> p;
        auto f = p.get_future();
        tp->submit([&p]
                   { p.set_value(); });
        f.wait();
    }

    /// 清理轮转残留文件(base / base_i / base_i.gz / base_i.gz.enc)
    void removeStaleRotated(const std::string &base)
    {
        std::remove(base.c_str());
        auto dot = base.rfind('.');
        std::string stem = (dot != std::string::npos) ? base.substr(0, dot) : base;
        std::string ext = (dot != std::string::npos) ? base.substr(dot) : std::string{};
        for (int i = 1; i <= static_cast<int>(kRotateMaxFiles); ++i)
            for (const char *sfx : {"", ".gz", ".gz.enc"})
                std::remove((stem + "_" + std::to_string(i) + ext + sfx).c_str());
    }

    /// 轮转 Sink: 每 1MB 轮转, 最多保留 3 份, 附旋转后处理链;
    /// 清理残留后从空文件追加写起(首轮 1MB 才触发轮转, 内容非空)
    std::shared_ptr<logger::LogSink> makeRotatingSink(const std::string &path,
                                                      std::unique_ptr<logger::RotatedFileHandler> chain)
    {
        removeStaleRotated(path);
        return std::make_shared<logger::RotatingFileLogSinkMT>(
            path, kRotateSize, kRotateMaxFiles, /*rotateOnOpen=*/false, std::move(chain));
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
        auto tp = std::make_shared<utils::ThreadPool>(poolThreads, kAsyncQueueSize);
        auto l = std::make_shared<logger::AsyncLogger>("bench_async", sink, tp);
        runLoop(l, kWarmup);
        waitDrain(tp.get());

        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            double t0 = nowSec();
            runLoop(l, kIters);
            waitDrain(tp.get());
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

    /// 端到端(含处理链): 计时窗口包含轮转池排空 —— 压缩速率跟不上
    /// 轮转速率时背压会传导至写路径, 测得的是含压缩链的可持续吞吐
    Result benchSyncE2E(const std::shared_ptr<logger::LogSink> &sink,
                        utils::ThreadPool *rotatePool)
    {
        auto l = std::make_shared<logger::Logger>("bench_e2e_sync", sink);
        runLoop(l, kWarmup);
        waitDrain(rotatePool); // 预热产生的压缩任务不计时

        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            double t0 = nowSec();
            runLoop(l, kIters);
            waitDrain(rotatePool); // 压缩链排空计入计时
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

    Result benchAsyncE2E(const std::shared_ptr<logger::LogSink> &sink,
                         utils::ThreadPool *rotatePool)
    {
        auto tp = std::make_shared<utils::ThreadPool>(1, kAsyncQueueSize);
        auto l = std::make_shared<logger::AsyncLogger>("bench_e2e_async", sink, tp);
        runLoop(l, kWarmup);
        waitDrain(tp.get());
        waitDrain(rotatePool);

        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            double t0 = nowSec();
            runLoop(l, kIters);
            waitDrain(tp.get());   // 日志后端写完
            waitDrain(rotatePool); // 压缩链排空计入计时
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

    /// 多线程并发写同一 Logger: 返回总吞吐(线程数×每线程条数/耗时)
    Result benchMultiSync(const std::shared_ptr<logger::LogSink> &sink)
    {
        auto l = std::make_shared<logger::Logger>("bench_mt_sync", sink);
        std::vector<std::thread> warm;
        for (int t = 0; t < kMtThreads; ++t)
            warm.emplace_back([&]
                              { runLoop(l, kMtWarmupPerThread); });
        for (auto &th : warm)
            th.join();

        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            std::vector<std::thread> ts;
            double t0 = nowSec();
            for (int t = 0; t < kMtThreads; ++t)
                ts.emplace_back([&]
                                { runLoop(l, kMtItersPerThread); });
            for (auto &th : ts)
                th.join();
            double sec = nowSec() - t0;

            double total = (double)kMtThreads * kMtItersPerThread;
            double ops = total / sec;
            if (ops > best.opsPerSec)
            {
                best.opsPerSec = ops;
                best.nsPerOp = 1e9 / ops;
            }
        }
        return best;
    }

    Result benchMultiAsync(const std::shared_ptr<logger::LogSink> &sink)
    {
        auto tp = std::make_shared<utils::ThreadPool>(1, kAsyncQueueSize);
        auto l = std::make_shared<logger::AsyncLogger>("bench_mt_async", sink, tp);
        std::vector<std::thread> warm;
        for (int t = 0; t < kMtThreads; ++t)
            warm.emplace_back([&]
                              { runLoop(l, kMtWarmupPerThread); });
        for (auto &th : warm)
            th.join();
        waitDrain(tp.get());

        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            std::vector<std::thread> ts;
            double t0 = nowSec();
            for (int t = 0; t < kMtThreads; ++t)
                ts.emplace_back([&]
                                { runLoop(l, kMtItersPerThread); });
            for (auto &th : ts)
                th.join();
            waitDrain(tp.get());
            double sec = nowSec() - t0;

            double total = (double)kMtThreads * kMtItersPerThread;
            double ops = total / sec;
            if (ops > best.opsPerSec)
            {
                best.opsPerSec = ops;
                best.nsPerOp = 1e9 / ops;
            }
        }
        return best;
    }

    /// 存储介质探测(仅常见类型, 供基准条件标注)
    const char *fsTypeName(const char *path)
    {
        struct statfs st;
        if (::statfs(path, &st) != 0)
            return "unknown";
        switch (st.f_type)
        {
        case 0x58465342: return "xfs";
        case 0x01021994: return "tmpfs";
        case 0xEF53: return "ext4";
        default: return "other";
        }
    }

    /// 统一输出: 场景名 + 配置矩阵(轮转/压缩/加密) + 吞吐 + 单条耗时 + 单轮时长
    void report(const char *scenario, const Result &r,
                bool rotate, bool compress, bool encrypt)
    {
        std::cout << std::left << std::setw(32) << scenario
                  << std::setw(7) << (rotate ? "Y" : "-")
                  << std::setw(7) << (compress ? "Y" : "-")
                  << std::setw(7) << (encrypt ? "Y" : "-")
                  << std::right << std::setw(13) << (long long)r.opsPerSec << " ops/s"
                  << std::setw(10) << (long long)r.nsPerOp << " ns/op"
                  << std::setw(9) << std::fixed << std::setprecision(2)
                  << (double)kIters / r.opsPerSec << " s\n"
                  << std::defaultfloat;
    }

} // namespace

int main()
{
    double tAll0 = nowSec();
    auto nullSink = std::make_shared<NullLogSink>();
    auto fileSink = std::make_shared<logger::BasicFileLogSinkMT>("/tmp/logger_bench.log", /*truncate=*/true);

    // 压缩链: zlib gzip; 压缩加密链: gzip → AES-256-GCM
    auto compressChain = logger::create_compress_handler();
    std::vector<std::unique_ptr<logger::RotatedFileHandler>> chain;
    chain.push_back(logger::create_compress_handler());
    chain.push_back(logger::create_encrypt_handler("bench-aes-key"));
    auto compressEncryptChain = logger::create_composite_handler(std::move(chain));

    auto rotCompressSink = makeRotatingSink("/tmp/logger_bench_rot.log", std::move(compressChain));
    auto rotEncSink = makeRotatingSink("/tmp/logger_bench_rotenc.log", std::move(compressEncryptChain));
    auto mtFileSink = std::make_shared<logger::BasicFileLogSinkMT>("/tmp/logger_bench_mt.log", /*truncate=*/true);

    std::cout << "==== 测试条件 ====\n";
    std::cout << "日志内容: \"bench message {i} with 3.14 args\", 单条约 82B(含默认 pattern 前缀, 实测均值)\n";
    std::cout << "格式化: 全部场景开启(默认 pattern: 时间戳+日志名+级别+消息)\n";
    std::cout << "存储介质: /tmp (" << fsTypeName("/tmp") << ")\n";
    std::cout << "线程数: 单线程场景 1 线程; 多线程场景 " << kMtThreads << " 线程 x "
              << kMtItersPerThread << " 条/线程\n";
    std::cout << "异步池: 1 线程 x " << kAsyncQueueSize
              << " 队列; 轮转压缩链另有全局单线程池(256 任务)\n";
    std::cout << "轮转: " << (kRotateSize >> 20) << "MB/次, 保留 " << kRotateMaxFiles
              << " 份; 压缩: zlib level9; 加密: AES-256-GCM\n";
    std::cout << "每场景: 预热 " << kWarmup << " 条 + " << kRounds << " 轮 x "
              << kIters << " 条(取最优轮); 多线程场景总量同为 " << kIters << " 条\n";
    std::cout << "注意: 各场景配置不同(轮转/压缩/加密/端到端语义), 吞吐不可跨场景直接比较\n\n";

    std::cout << std::left << std::setw(32) << "场景"
              << std::setw(7) << "轮转"
              << std::setw(7) << "压缩"
              << std::setw(7) << "加密"
              << std::right << std::setw(13) << "吞吐"
              << std::setw(14) << "单条耗时"
              << std::setw(14) << "单轮时长\n";
    std::cout << std::string(94, '-') << "\n";

    report("同步 + NullSink", benchSync(nullSink), false, false, false);
    report("同步 + FileSink(落盘)", benchSync(fileSink), false, false, false);
    report("异步 + NullSink(1后台线程)", benchAsync(nullSink, 1), false, false, false);
    report("异步 + NullSink(4后台线程)", benchAsync(nullSink, 4), false, false, false);
    report("异步 + FileSink(1后台线程)", benchAsync(fileSink, 1), false, false, false);
    auto *rotatePool = logger::RotationStrategy::rotating_pool();
    report("同步 + 轮转FileSink + 压缩链", benchSync(rotCompressSink), true, true, false);
    waitDrain(rotatePool); // 排空压缩任务, 隔离场景
    report("同步 + 轮转FileSink + 压缩加密链", benchSync(rotEncSink), true, true, true);
    waitDrain(rotatePool);
    report("异步 + 轮转FileSink + 压缩链", benchAsync(rotCompressSink, 1), true, true, false);
    waitDrain(rotatePool);
    report("异步 + 轮转FileSink + 压缩加密链", benchAsync(rotEncSink, 1), true, true, true);
    waitDrain(rotatePool);
    report("同步 + 轮转 + 压缩链(端到端)", benchSyncE2E(rotCompressSink, rotatePool), true, true, false);
    waitDrain(rotatePool);
    report("异步 + 轮转 + 压缩链(端到端)", benchAsyncE2E(rotCompressSink, rotatePool), true, true, false);
    waitDrain(rotatePool);
    report("多线程同步 + FileSink(MT)", benchMultiSync(mtFileSink), false, false, false);
    report("多线程异步 + FileSink", benchMultiAsync(mtFileSink), false, false, false);

    std::cout << "总耗时: " << std::fixed << std::setprecision(1)
              << nowSec() - tAll0 << " s\n";
    return 0;
}

/**
 * mpmc_blocking_queue(互斥+条件变量) vs mpmc_bounded_queue(无锁) 吞吐对比
 *
 * 场景: SPSC(1产1消) / MPSC(4产1消) / MPMC(8产8消)
 * 两个队列均使用各自自然用法:
 * - blocking: 阻塞 enqueue/dequeue(满/空时条件变量等待)
 * - bounded : 满/空时自旋 + yield 重试
 * 每个场景跑 3 轮取最优, 并校验消费总和(检测丢/重元素)。
 */
#include "utils/mpmc_blocking_queue.h"
#include "utils/mpmc_bounded_queue.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
    constexpr size_t kCapacity = 1024;
    constexpr uint64_t kTotalOps = 4'000'000;
    constexpr int kRounds = 3;

    struct Result
    {
        double opsPerSec = 0;
        double nsPerOp = 0;
        bool verifyOk = false;
    };

    // 计算期望总和: 生产者 p 产出 [p*base, p*base+base) 连续整数
    uint64_t expectedSum(int producers)
    {
        uint64_t base = kTotalOps / producers;
        uint64_t s = base * (base - 1) / 2;               // 每个生产者区间内和
        uint64_t off = (uint64_t)(producers - 1) * producers / 2 * base * base; // 区间偏移和
        return producers * s + off;
    }

    // ---- 阻塞队列 ----
    Result benchBlocking(int producers, int consumers, uint64_t expected)
    {
        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            utils::mpmc_blocking_queue<uint64_t> q(kCapacity);
            std::atomic<uint64_t> consumed{0};
            std::atomic<uint64_t> sum{0};

            auto t0 = std::chrono::steady_clock::now();
            std::vector<std::thread> ts;
            for (int p = 0; p < producers; ++p)
            {
                ts.emplace_back([&, p]
                                {
                                    uint64_t base = (uint64_t)p * (kTotalOps / producers);
                                    for (uint64_t i = 0; i < kTotalOps / producers; ++i)
                                        q.enqueue(base + i);
                                });
            }
            for (int c = 0; c < consumers; ++c)
            {
                ts.emplace_back([&]
                                {
                                    while (consumed.load(std::memory_order_relaxed) < kTotalOps)
                                    {
                                        auto v = q.dequeue_for(std::chrono::milliseconds(10));
                                        if (!v)
                                            continue;
                                        sum.fetch_add(*v, std::memory_order_relaxed);
                                        consumed.fetch_add(1, std::memory_order_relaxed);
                                    }
                                });
            }
            for (auto &t : ts)
                t.join();
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

            double ops = (double)kTotalOps / sec;
            if (ops > best.opsPerSec)
            {
                best.opsPerSec = ops;
                best.nsPerOp = 1e9 / ops;
                best.verifyOk = (sum.load() == expected && consumed.load() == kTotalOps);
            }
        }
        return best;
    }

    // ---- 无锁有界队列 ----
    Result benchBounded(int producers, int consumers, uint64_t expected)
    {
        Result best;
        for (int r = 0; r < kRounds; ++r)
        {
            utils::mpmc_bounded_queue<uint64_t> q(kCapacity);
            std::atomic<uint64_t> consumed{0};
            std::atomic<uint64_t> sum{0};

            auto t0 = std::chrono::steady_clock::now();
            std::vector<std::thread> ts;
            for (int p = 0; p < producers; ++p)
            {
                ts.emplace_back([&, p]
                                {
                                    uint64_t base = (uint64_t)p * (kTotalOps / producers);
                                    for (uint64_t i = 0; i < kTotalOps / producers; ++i)
                                        while (!q.enqueue(base + i))
                                            std::this_thread::yield();
                                });
            }
            for (int c = 0; c < consumers; ++c)
            {
                ts.emplace_back([&]
                                {
                                    while (consumed.load(std::memory_order_relaxed) < kTotalOps)
                                    {
                                        uint64_t v = 0;
                                        if (!q.dequeue(v))
                                        {
                                            std::this_thread::yield();
                                            continue;
                                        }
                                        sum.fetch_add(v, std::memory_order_relaxed);
                                        consumed.fetch_add(1, std::memory_order_relaxed);
                                    }
                                });
            }
            for (auto &t : ts)
                t.join();
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

            double ops = (double)kTotalOps / sec;
            if (ops > best.opsPerSec)
            {
                best.opsPerSec = ops;
                best.nsPerOp = 1e9 / ops;
                best.verifyOk = (sum.load() == expected && consumed.load() == kTotalOps);
            }
        }
        return best;
    }

    void report(const char *scenario, const Result &blk, const Result &bnd)
    {
        std::cout << std::left << std::setw(14) << scenario
                  << std::setw(10) << "blocking"
                  << std::right << std::setw(14) << (long long)blk.opsPerSec << " ops/s"
                  << std::setw(10) << (long long)blk.nsPerOp << " ns/op"
                  << (blk.verifyOk ? "" : "  [校验失败!]") << "\n";
        std::cout << std::left << std::setw(14) << ""
                  << std::setw(10) << "bounded"
                  << std::right << std::setw(14) << (long long)bnd.opsPerSec << " ops/s"
                  << std::setw(10) << (long long)bnd.nsPerOp << " ns/op"
                  << (bnd.verifyOk ? "" : "  [校验失败!]") << "\n";
    }

    void benchScenario(int producers, int consumers, const char *name)
    {
        uint64_t expected = expectedSum(producers);
        // 预热一轮, 排除冷启动噪声
        benchBlocking(producers, consumers, expected);
        benchBounded(producers, consumers, expected);

        auto blk = benchBlocking(producers, consumers, expected);
        auto bnd = benchBounded(producers, consumers, expected);
        report(name, blk, bnd);
    }

} // namespace

int main()
{
    std::cout << "队列容量: " << kCapacity << ", 每场景总操作: " << kTotalOps
              << ", 轮数(取最优): " << kRounds << "\n\n";
    std::cout << std::left << std::setw(14) << "场景"
              << std::setw(10) << "实现"
              << std::right << std::setw(14) << "吞吐"
              << std::setw(14) << "单次耗时\n";
    std::cout << std::string(52, '-') << "\n";

    benchScenario(1, 1, "SPSC(1P1C)");
    benchScenario(4, 1, "MPSC(4P1C)");
    benchScenario(8, 8, "MPMC(8P8C)");
    return 0;
}

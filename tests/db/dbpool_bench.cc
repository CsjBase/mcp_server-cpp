/**
 * @file dbpool_bench.cc
 * @brief 连接池借/还延迟与并发吞吐基准(注入 MockDBFactory, 无需真实 MySQL)
 *
 * 场景:
 *   - 单线程串行借还: 测量借出+归还原语的纯延迟(ns/op),
 *     包含无锁 dequeue/enqueue、原子状态切换、shared_ptr 创建/析构
 *   - 4/8 线程并发借还: 测量多线程竞争下的总吞吐
 * 说明: 借出后空闲数低于 minIdle 会触发维护线程补充(生产环境正常行为),
 * 单线程场景该补充在后台线程执行, 不阻塞借出路径。
 * 每场景预热 1 轮 + 3 轮取最优。
 */
#include "db/DBPool.h"
#include "mock_db.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
    constexpr int kItersSingle = 1'000'000;
    constexpr int kItersPerThread = 500'000;
    constexpr int kRounds = 3;

    inline double nowSec()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    DBPoolConfig makeConfig(int maxSize, int minIdle)
    {
        DBPoolConfig cfg = DBPoolConfig::defaultConfig();
        cfg.max_pool_size = maxSize;
        cfg.min_idle = minIdle;
        cfg.get_connection_timeout_ms = 5000;
        cfg.idle_timeout_seconds = 3600;
        cfg.max_life_time_seconds = 3600;
        cfg.keepalive_seconds = 3600;
        cfg.scan_interval_ms = 3600'000; // 基准期间避免周期扫描干扰
        return cfg;
    }

    void runBorrowLoop(DBPool &pool, int iters, std::atomic<int> &failures)
    {
        for (int i = 0; i < iters; ++i)
        {
            auto conn = pool.getConnection();
            if (!conn)
                failures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /// 单线程串行借还: 返回 ns/op
    double benchSingle(DBPool &pool, double &opsPerSec)
    {
        double bestNs = 1e18;
        std::atomic<int> failures{0};
        runBorrowLoop(pool, 100'000, failures); // 预热

        for (int r = 0; r < kRounds; ++r)
        {
            double t0 = nowSec();
            runBorrowLoop(pool, kItersSingle, failures);
            double sec = nowSec() - t0;

            double ns = sec * 1e9 / kItersSingle;
            if (ns < bestNs)
            {
                bestNs = ns;
                opsPerSec = kItersSingle / sec;
            }
        }
        return bestNs;
    }

    /// 多线程并发借还: 返回总 ops/s
    double benchConcurrent(DBPool &pool, int threads, double &nsPerOp)
    {
        double bestOps = 0;
        std::atomic<int> failures{0};

        auto run = [&]()
        {
            std::vector<std::thread> ts;
            double t0 = nowSec();
            for (int t = 0; t < threads; ++t)
                ts.emplace_back([&] { runBorrowLoop(pool, kItersPerThread, failures); });
            for (auto &t : ts)
                t.join();
            return nowSec() - t0;
        };

        run(); // 预热
        for (int r = 0; r < kRounds; ++r)
        {
            double sec = run();
            double ops = (double)threads * kItersPerThread / sec;
            if (ops > bestOps)
            {
                bestOps = ops;
                nsPerOp = 1e9 / ops;
            }
        }
        return bestOps;
    }

} // namespace

int main()
{
    auto factory = std::make_shared<test::MockDBFactory>();
    DBPool pool(factory, makeConfig(/*maxSize=*/256, /*minIdle=*/32));

    std::cout << "连接池: maxSize=256, minIdle=32, Mock 连接(无网络 I/O)\n\n";

    double opsPerSec = 0;
    double ns = benchSingle(pool, opsPerSec);
    std::cout << std::left << std::setw(28) << "单线程串行借还"
              << std::right << std::setw(10) << (long long)ns << " ns/op"
              << std::setw(16) << (long long)opsPerSec << " ops/s\n";

    double nsPerOp = 0;
    double ops4 = benchConcurrent(pool, 4, nsPerOp);
    std::cout << std::left << std::setw(28) << "4线程并发借还"
              << std::right << std::setw(10) << (long long)nsPerOp << " ns/op"
              << std::setw(16) << (long long)ops4 << " ops/s\n";

    double nsPerOp8 = 0;
    double ops8 = benchConcurrent(pool, 8, nsPerOp8);
    std::cout << std::left << std::setw(28) << "8线程并发借还"
              << std::right << std::setw(10) << (long long)nsPerOp8 << " ns/op"
              << std::setw(16) << (long long)ops8 << " ops/s\n";
    return 0;
}

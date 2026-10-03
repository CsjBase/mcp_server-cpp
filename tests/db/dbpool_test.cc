#include "db/DBPool.h"
#include "db/MySQL.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <random>
#include <iostream>
#include <algorithm>

using namespace db;

struct Stats
{
    std::atomic<uint64_t> total{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> latencySumUs{0};
    std::vector<uint64_t> latencies; // 每线程一份，最后合并
    std::mutex mu;

    void record(uint64_t us, bool err)
    {
        ++total;
        if (err)
            ++errors;
        latencySumUs += us;
        std::lock_guard<std::mutex> lk(mu);
        latencies.push_back(us);
    }

    void report(int seconds)
    {
        auto t = total.load();
        auto e = errors.load();
        double qps = t / (double)seconds;
        double avgUs = t ? (double)latencySumUs.load() / t : 0;

        std::sort(latencies.begin(), latencies.end());
        auto pct = [&](double p) -> uint64_t
        {
            if (latencies.empty())
                return 0;
            size_t i = (size_t)(p * latencies.size());
            if (i >= latencies.size())
                i = latencies.size() - 1;
            return latencies[i];
        };

        std::cout << "==================== Stress Report ====================\n"
                  << "  total   : " << t << "\n"
                  << "  errors  : " << e << " (" << (t ? 100.0 * e / t : 0) << "%)\n"
                  << "  QPS     : " << qps << "\n"
                  << "  avg(us) : " << avgUs << "\n"
                  << "  p50(us) : " << pct(0.50) << "\n"
                  << "  p90(us) : " << pct(0.90) << "\n"
                  << "  p99(us) : " << pct(0.99) << "\n"
                  << "  p999(us): " << pct(0.999) << "\n"
                  << "  max(us) : " << (latencies.empty() ? 0 : latencies.back()) << "\n"
                  << "======================================================\n";
    }
};

int main()
{
    int threads = 16;
    int seconds = 30;
    int readRatio = 80; // 80% 读
    auto &pool = DBPool::instance();
    // 准备数据
    {
        auto conn = pool.getConnection();
        conn->execute("CREATE TABLE IF NOT EXISTS t_big ("
                      "id   BIGINT PRIMARY KEY AUTO_INCREMENT,"
                      "data VARCHAR(256) NOT NULL"
                      ") ENGINE=InnoDB;");
        conn->execute("TRUNCATE TABLE t_big");
        for (int i = 0; i < 10000; ++i)
            conn->execute("INSERT INTO t_big(data) VALUES('seed')");
    }

    Stats stats;
    std::atomic<bool> running{true};
    std::vector<std::thread> ts;

    auto worker = [&](int tid)
    {
        std::mt19937 rng(tid);
        std::uniform_int_distribution<int> dist(0, 99);
        while (running)
        {
            auto begin = std::chrono::steady_clock::now();
            auto conn = pool.getConnection();
            bool err = false;
            if (!conn)
            {
                err = true;
            }
            else if (dist(rng) < readRatio)
            {
                auto res = conn->query("SELECT id,data FROM t_big LIMIT 10");
                err = (res == nullptr);
            }
            else
            {
                err = (conn->execute("INSERT INTO t_big(data) VALUES('stress')") != 0);
            }
            auto end = std::chrono::steady_clock::now();
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
            stats.record((uint64_t)us, err);
        }
    };

    for (int i = 0; i < threads; ++i)
        ts.emplace_back(worker, i);
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    running = false;
    for (auto &t : ts)
        t.join();

    stats.report(seconds);
    return 0;
}
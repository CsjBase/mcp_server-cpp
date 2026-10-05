/**
 * @file cached_clock_test.cc
 * @brief CachedClock 单元测试: 精度偏差、单调性、时间推进、并发读
 */
#include "utils/cached_clock.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace utils;

// 与真实时钟偏差: 刷新周期 ~1ms, 容忍 50ms 兜底慢环境
TEST(CachedClock, WallNowCloseToRealClock)
{
    auto cached = cached_wall_now();
    auto real = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(real - cached).count();
    EXPECT_LT(std::llabs(diff), 50);
}

TEST(CachedClock, SteadyNowCloseToRealClock)
{
    auto cached = cached_steady_now();
    auto real = std::chrono::steady_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(real - cached).count();
    EXPECT_LT(std::llabs(diff), 50);
}

// 单调不倒退
TEST(CachedClock, SteadyNowNeverGoesBackward)
{
    auto prev = cached_steady_now();
    for (int i = 0; i < 1000; ++i)
    {
        auto cur = cached_steady_now();
        EXPECT_GE(cur, prev);
        prev = cur;
    }
}

// 时间推进: 睡 30ms 后应至少前进 20ms(容差 10ms)
TEST(CachedClock, SteadyNowAdvances)
{
    auto before = cached_steady_now();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    auto after = cached_steady_now();
    auto advanced = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();
    EXPECT_GE(advanced, 20);
}

// 并发读: 4 线程各读 1M 次, 验证不崩溃且读到的时间不小于构造时刻
TEST(CachedClock, ConcurrentReads)
{
    auto baseline = cached_steady_now();
    std::vector<std::thread> ts;
    std::atomic<int> errors{0};
    for (int t = 0; t < 4; ++t)
    {
        ts.emplace_back([&]
                        {
                            for (int i = 0; i < 1'000'000; ++i)
                            {
                                if (cached_steady_now() < baseline)
                                    errors.fetch_add(1, std::memory_order_relaxed);
                            }
                        });
    }
    for (auto &t : ts)
        t.join();
    EXPECT_EQ(errors.load(), 0);
}

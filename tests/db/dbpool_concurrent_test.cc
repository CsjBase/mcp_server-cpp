/**
 * DBPool 并发正确性测试:
 * 验证无锁借/还(MPMC 空闲队列 + 池空等待协议)在多线程竞争下
 * 不丢连接、不重复借出, 以及超时/保活/收缩/生命周期剔除行为。
 * 使用 MockDB 工厂注入, 不依赖真实 MySQL。
 */
#include "db/DBPool.h"
#include "mock_db.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace db;

namespace
{
    // 构造测试配置: 时间参数全部可调
    DBPoolConfig makeConfig(int maxSize, int minIdle, int timeoutMs = 1000,
                            int idleSec = 3600, int lifeSec = 3600,
                            int keepaliveSec = 3600, int scanMs = 100)
    {
        DBPoolConfig cfg = DBPoolConfig::defaultConfig();
        cfg.max_pool_size = maxSize;
        cfg.min_idle = minIdle;
        cfg.get_connection_timeout_ms = timeoutMs;
        cfg.idle_timeout_seconds = idleSec;
        cfg.max_life_time_seconds = lifeSec;
        cfg.keepalive_seconds = keepaliveSec;
        cfg.scan_interval_ms = scanMs;
        return cfg;
    }

    // 轮询等待条件成立(避免对维护线程时序做硬编码)
    bool waitUntil(const std::function<bool()> &cond, int timeoutMs)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (cond())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return cond();
    }
} // namespace

// 多线程竞争借还: 同一连接不允许被并发借出, 结束后无丢失
TEST(DBPoolConcurrent, BorrowReleaseNoDuplicateUnderContention)
{
    auto factory = std::make_shared<test::MockDBFactory>();
    DBPool pool(factory, makeConfig(8, 2));

    std::mutex m;
    std::unordered_map<DBConnection *, int> inflight; // 连接 → 并发借出次数
    std::atomic<int> errors{0};
    std::atomic<int> nullConns{0};

    constexpr int kThreads = 8;
    constexpr int kIters = 3000;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t)
    {
        ts.emplace_back([&]
        {
            for (int i = 0; i < kIters; ++i)
            {
                auto conn = pool.getConnection();
                if (!conn)
                {
                    ++nullConns;
                    continue;
                }
                DBConnection *raw = conn.get();
                {
                    std::lock_guard<std::mutex> lk(m);
                    if (++inflight[raw] > 1)
                        ++errors; // 同一连接被并发借出
                }
                conn->execute("SELECT 1");
                {
                    std::lock_guard<std::mutex> lk(m);
                    if (--inflight[raw] == 0)
                        inflight.erase(raw);
                }
            }
        });
    }
    for (auto &t : ts)
        t.join();

    EXPECT_EQ(errors.load(), 0);
    EXPECT_EQ(nullConns.load(), 0);
    // 全部归还且无丢失(等待在途的异步创建完成)
    EXPECT_TRUE(waitUntil([&]
                          {
                              auto st = pool.stats();
                              return st.inUse == 0 && st.idle == st.total;
                          },
                          3000));
    auto st = pool.stats();
    EXPECT_EQ(st.inUse, 0);
    EXPECT_EQ(st.idle, st.total);
    EXPECT_GE(st.total, 2);
}

// 池满且全部借出: 借出等待超时返回 nullptr; 归还后可立即借到
TEST(DBPoolConcurrent, TimeoutWhenExhausted)
{
    auto factory = std::make_shared<test::MockDBFactory>();
    DBPool pool(factory, makeConfig(2, 1, /*timeoutMs=*/150));

    auto c1 = pool.getConnection();
    auto c2 = pool.getConnection(); // 触发补充 → 维护线程创建第二个
    ASSERT_TRUE(c1);
    ASSERT_TRUE(c2);

    // 2 个连接全部借出 → 第三次借出超时
    auto t0 = std::chrono::steady_clock::now();
    auto c3 = pool.getConnection();
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t0)
                         .count();
    EXPECT_EQ(c3, nullptr);
    EXPECT_GE(elapsedMs, 100);
    EXPECT_LT(elapsedMs, 1000);

    c1.reset(); // 归还一个 → 立即可借
    auto c4 = pool.getConnection();
    ASSERT_TRUE(c4);
}

// 空闲连接达到 keepalive 阈值后, 维护线程应对其 ping 且保留可用
TEST(DBPoolConcurrent, KeepalivePingForIdleConn)
{
    auto factory = std::make_shared<test::MockDBFactory>();
    DBPool pool(factory, makeConfig(2, 1, 1000, 3600, 3600, /*keepaliveSec=*/1, /*scanMs=*/100));

    {
        auto conn = pool.getConnection();
        ASSERT_TRUE(conn);
        conn->execute("SELECT 1");
    } // 归还 → 空闲

    auto created = factory->created();
    ASSERT_EQ(created.size(), 1u);
    // 等待维护线程对其执行保活 ping
    EXPECT_TRUE(waitUntil([&]
                          { return created[0]->pingCount() >= 1; },
                          3000));

    auto st = pool.stats();
    EXPECT_EQ(st.total, 1); // ping 成功, 未被剔除
    auto conn2 = pool.getConnection();
    ASSERT_TRUE(conn2); // 仍可借
}

// 空闲连接数超过 minIdle 且空闲超时 → 维护线程收缩到 minIdle
TEST(DBPoolConcurrent, ShrinkIdleAboveMinIdle)
{
    auto factory = std::make_shared<test::MockDBFactory>();
    DBPool pool(factory, makeConfig(2, 1, 1000, /*idleSec=*/1, 3600, 3600, 100));

    auto c1 = pool.getConnection();
    auto c2 = pool.getConnection(); // 触发补充 → 共 2 个连接
    ASSERT_TRUE(c1);
    ASSERT_TRUE(c2);
    c1.reset();
    c2.reset(); // 2 个空闲, 超过 minIdle=1

    // 空闲 1s 后, 维护线程应剔除多余空闲连接, 收缩到 minIdle
    EXPECT_TRUE(waitUntil([&]
                          { return pool.stats().total == 1; },
                          3000));
    auto st = pool.stats();
    EXPECT_EQ(st.total, 1);
    EXPECT_EQ(st.idle, 1);
}

// 连接超过 maxLifetime 后被剔除并补充新连接
TEST(DBPoolConcurrent, EvictExpiredLifetimeConn)
{
    auto factory = std::make_shared<test::MockDBFactory>();
    DBPool pool(factory, makeConfig(2, 1, 1000, 3600, /*lifeSec=*/1, 3600, 100));

    auto c1 = pool.getConnection();
    ASSERT_TRUE(c1);
    auto raw0 = c1->getRaw().get();
    c1.reset(); // 归还(此时未过期)

    // 超过生命周期后被剔除, 并补充新连接
    EXPECT_TRUE(waitUntil([&]
                          { return factory->createdCount() >= 2; },
                          3000));

    auto c2 = pool.getConnection();
    ASSERT_TRUE(c2);
    EXPECT_NE(c2->getRaw().get(), raw0); // 是新建连接, 不是过期连接
}

// 归还损坏连接 → 剔除并补充, 池恢复可用, 损坏连接不再被借出
TEST(DBPoolConcurrent, DiscardBrokenConnOnRelease)
{
    auto factory = std::make_shared<test::MockDBFactory>();
    DBPool pool(factory, makeConfig(2, 1, 1000));

    auto c1 = pool.getConnection();
    ASSERT_TRUE(c1);
    auto mock = std::dynamic_pointer_cast<test::MockDB>(c1->getRaw());
    ASSERT_TRUE(mock);
    mock->setValid(false); // 模拟连接损坏
    auto brokenRaw = mock.get();
    c1.reset(); // 归还时发现失效 → 剔除

    // 补充完成后池恢复可用(剔除与补充的竞态下总数不固定, 只校验可用性)
    EXPECT_TRUE(waitUntil([&]
                          { return pool.stats().idle >= 1; },
                          3000));
    auto c2 = pool.getConnection();
    ASSERT_TRUE(c2);
    EXPECT_NE(c2->getRaw().get(), brokenRaw); // 损坏连接已被剔除, 不会再次借出
}

#include "utils/mpmc_blocking_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using utils::mpmc_blocking_queue;

// ---- default construction ----
TEST(MpmcBlockingQueueTest, DefaultCtor)
{
    mpmc_blocking_queue<int> q;
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0);
    EXPECT_EQ(q.overrun_counter(), 0);
    EXPECT_EQ(q.discard_counter(), 0);
}

// ---- sized construction ----
TEST(MpmcBlockingQueueTest, SizedCtor)
{
    mpmc_blocking_queue<int> q(4);
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0);
    EXPECT_EQ(q.overrun_counter(), 0);
    EXPECT_EQ(q.discard_counter(), 0);
}

// ---- basic enqueue / dequeue (blocking) ----
TEST(MpmcBlockingQueueTest, EnqueueDequeue)
{
    mpmc_blocking_queue<int> q(4);

    q.enqueue(10);
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1);

    q.enqueue(20);
    q.enqueue(30);
    EXPECT_EQ(q.size(), 3);

    EXPECT_EQ(*q.dequeue(), 10);
    EXPECT_EQ(q.size(), 2);
    EXPECT_EQ(*q.dequeue(), 20);
    EXPECT_EQ(*q.dequeue(), 30);
    EXPECT_TRUE(q.empty());
}

// ---- enqueue with rvalue reference ----
TEST(MpmcBlockingQueueTest, EnqueueRvalue)
{
    mpmc_blocking_queue<std::string> q(4);

    q.enqueue(std::string("hello"));
    q.enqueue(std::string("world"));

    EXPECT_EQ(*q.dequeue(), "hello");
    EXPECT_EQ(*q.dequeue(), "world");
}

// ---- enqueue_overwrite: overwrites oldest when full ----
TEST(MpmcBlockingQueueTest, EnqueueOverwrite)
{
    mpmc_blocking_queue<int> q(3);

    q.enqueue_overwrite(1);
    q.enqueue_overwrite(2);
    q.enqueue_overwrite(3);
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 3);
    EXPECT_EQ(q.overrun_counter(), 0);

    q.enqueue_overwrite(4);
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 3);
    EXPECT_EQ(q.overrun_counter(), 1);
    EXPECT_EQ(*q.dequeue(), 2); // 1 was overwritten
    EXPECT_EQ(*q.dequeue(), 3);
    EXPECT_EQ(*q.dequeue(), 4);

    q.enqueue_overwrite(5);
    q.enqueue_overwrite(6);
    q.enqueue_overwrite(7);
    q.enqueue_overwrite(8);            // overwrites 5
    EXPECT_EQ(q.overrun_counter(), 2); // 1st overwrite + 2nd overwrite = 2
    EXPECT_EQ(*q.dequeue(), 6);
    EXPECT_EQ(*q.dequeue(), 7);
    EXPECT_EQ(*q.dequeue(), 8);
}

// ---- try_enqueue succeeds when not full ----
TEST(MpmcBlockingQueueTest, TryEnqueueSuccess)
{
    mpmc_blocking_queue<int> q(2);

    EXPECT_TRUE(q.try_enqueue(1));
    EXPECT_TRUE(q.try_enqueue(2));
    EXPECT_EQ(q.size(), 2);
    EXPECT_TRUE(q.full());
}

// ---- try_enqueue fails when full ----
TEST(MpmcBlockingQueueTest, TryEnqueueFull)
{
    mpmc_blocking_queue<int> q(2);

    q.enqueue(1);
    q.enqueue(2);
    EXPECT_FALSE(q.try_enqueue(3));
    EXPECT_EQ(q.size(), 2);
    EXPECT_EQ(*q.dequeue(), 1);
    EXPECT_EQ(*q.dequeue(), 2);
}

// ---- try_dequeue succeeds when not empty ----
TEST(MpmcBlockingQueueTest, TryDequeueSuccess)
{
    mpmc_blocking_queue<int> q(2);

    q.enqueue(42);
    auto v = q.try_dequeue();
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 42);
    EXPECT_TRUE(q.empty());
}

// ---- try_dequeue returns nullopt when empty ----
TEST(MpmcBlockingQueueTest, TryDequeueEmpty)
{
    mpmc_blocking_queue<int> q(2);

    auto v = q.try_dequeue();
    EXPECT_FALSE(v.has_value());
}

// ---- dequeue_for with data immediately available ----
TEST(MpmcBlockingQueueTest, DequeueForImmediate)
{
    mpmc_blocking_queue<int> q(2);

    q.enqueue(99);
    auto v = q.dequeue_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 99);
}

// ---- dequeue_for timeout on empty queue ----
TEST(MpmcBlockingQueueTest, DequeueForTimeout)
{
    mpmc_blocking_queue<int> q(2);

    auto start = std::chrono::steady_clock::now();
    auto v = q.dequeue_for(std::chrono::milliseconds(50));
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(v.has_value());
    EXPECT_GE(elapsed, std::chrono::milliseconds(50));
}

// ---- dequeue_for wakes up when data arrives ----
TEST(MpmcBlockingQueueTest, DequeueForWokenByProducer)
{
    mpmc_blocking_queue<int> q(2);

    std::thread producer([&q]
                         {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        q.enqueue(7); });

    auto v = q.dequeue_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 7);

    producer.join();
}

// ---- blocking enqueue waits until space available ----
TEST(MpmcBlockingQueueTest, EnqueueBlocksUntilSpace)
{
    mpmc_blocking_queue<int> q(2);

    // Fill the queue
    q.enqueue(1);
    q.enqueue(2);

    std::atomic<bool> enqueued{false};

    std::thread producer([&q, &enqueued]
                         {
        q.enqueue(3);       // blocks until consumer pops
        enqueued.store(true); });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_FALSE(enqueued.load());

    q.dequeue(); // free one slot, queue becomes [2]
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_TRUE(enqueued.load()); // producer has enqueued 3, queue now [2, 3]

    EXPECT_EQ(*q.dequeue(), 2);
    EXPECT_EQ(*q.dequeue(), 3);

    producer.join();
}

// ---- blocking dequeue waits until item available ----
TEST(MpmcBlockingQueueTest, DequeueBlocksUntilItem)
{
    mpmc_blocking_queue<int> q(2);

    std::atomic<int> result{-1};
    std::atomic<bool> dequeued{false};

    std::thread consumer([&q, &result, &dequeued]
                         {
        result.store(*q.dequeue());
        dequeued.store(true); });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_FALSE(dequeued.load());

    q.enqueue(55);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_TRUE(dequeued.load());
    EXPECT_EQ(result.load(), 55);

    consumer.join();
}

// ---- overrun_counter and reset ----
TEST(MpmcBlockingQueueTest, OverrunCounter)
{
    mpmc_blocking_queue<int> q(2);

    EXPECT_EQ(q.overrun_counter(), 0);

    q.enqueue_overwrite(1);
    q.enqueue_overwrite(2);
    q.enqueue_overwrite(3); // overwrite 1
    q.enqueue_overwrite(4); // overwrite 2

    EXPECT_EQ(q.overrun_counter(), 2);

    q.reset_overrun_counter();
    EXPECT_EQ(q.overrun_counter(), 0);
}

// ---- discard_counter: tracks try_enqueue failures ----
TEST(MpmcBlockingQueueTest, DiscardCounterInitial)
{
    mpmc_blocking_queue<int> q(2);
    EXPECT_EQ(q.discard_counter(), 0);
}

TEST(MpmcBlockingQueueTest, DiscardCounterIncrementsOnTryEnqueueFail)
{
    mpmc_blocking_queue<int> q(2);

    q.enqueue(1);
    q.enqueue(2);
    EXPECT_TRUE(q.full());

    EXPECT_FALSE(q.try_enqueue(3));
    EXPECT_EQ(q.discard_counter(), 1);

    EXPECT_FALSE(q.try_enqueue(4));
    EXPECT_EQ(q.discard_counter(), 2);
}

TEST(MpmcBlockingQueueTest, DiscardCounterUnaffectedByTryEnqueueSuccess)
{
    mpmc_blocking_queue<int> q(2);

    EXPECT_TRUE(q.try_enqueue(1));
    EXPECT_EQ(q.discard_counter(), 0);

    EXPECT_TRUE(q.try_enqueue(2));
    EXPECT_EQ(q.discard_counter(), 0);
}

TEST(MpmcBlockingQueueTest, ResetDiscardCounter)
{
    mpmc_blocking_queue<int> q(1);

    q.enqueue(1);
    EXPECT_FALSE(q.try_enqueue(2));
    EXPECT_FALSE(q.try_enqueue(3));
    EXPECT_EQ(q.discard_counter(), 2);

    q.reset_discard_counter();
    EXPECT_EQ(q.discard_counter(), 0);
}

TEST(MpmcBlockingQueueTest, DiscardCounterIndependentFromOverrun)
{
    mpmc_blocking_queue<int> q(2);

    // try_enqueue failure increments discard_counter, not overrun_counter
    q.enqueue(1);
    q.enqueue(2);
    q.try_enqueue(3); // discarded
    EXPECT_EQ(q.discard_counter(), 1);
    EXPECT_EQ(q.overrun_counter(), 0);

    // enqueue_overwrite increments overrun_counter, not discard_counter
    q.enqueue_overwrite(4);            // overwrites 1
    EXPECT_EQ(q.discard_counter(), 1); // unchanged
    EXPECT_EQ(q.overrun_counter(), 1);
}

TEST(MpmcBlockingQueueTest, DiscardCounterRvalue)
{
    mpmc_blocking_queue<std::string> q(1);

    q.enqueue(std::string("hello"));
    EXPECT_FALSE(q.try_enqueue(std::string("world")));
    EXPECT_EQ(q.discard_counter(), 1);
}

// ---- move-only type (std::unique_ptr) ----
TEST(MpmcBlockingQueueTest, UniquePtrType)
{
    mpmc_blocking_queue<std::unique_ptr<int>> q(3);

    q.enqueue(std::make_unique<int>(100));
    q.enqueue(std::make_unique<int>(200));

    auto p1 = std::move(*q.dequeue());
    EXPECT_EQ(*p1, 100);
    auto p2 = std::move(*q.dequeue());
    EXPECT_EQ(*p2, 200);
}

// ---- copy semantics for enqueue ----
TEST(MpmcBlockingQueueTest, CopyEnqueue)
{
    mpmc_blocking_queue<std::string> q(3);

    std::string s = "unchanged";
    q.enqueue(s);
    s = "changed";

    auto v = *q.dequeue();
    EXPECT_EQ(v, "unchanged");
}

// ---- fill to capacity and drain ----
TEST(MpmcBlockingQueueTest, FillAndDrain)
{
    mpmc_blocking_queue<int> q(8);

    for (int i = 0; i < 8; ++i)
        q.enqueue(i);

    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 8);

    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(*q.dequeue(), i);

    EXPECT_TRUE(q.empty());
}

// ---- queue state queries are thread-safe ----
TEST(MpmcBlockingQueueTest, StateQueries)
{
    mpmc_blocking_queue<int> q(3);

    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0);

    q.enqueue(1);
    EXPECT_FALSE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 1);

    q.enqueue(2);
    q.enqueue(3);
    EXPECT_TRUE(q.full());
}

// ---- multi-producer multi-consumer stress test ----
TEST(MpmcBlockingQueueTest, MpmcStress)
{
    constexpr int kNumProducers = 4;
    constexpr int kNumConsumers = 4;
    constexpr int kItemsPerProducer = 1000;

    mpmc_blocking_queue<int> q(64);

    std::atomic<int> sum{0};
    std::atomic<int> produced_count{0};
    std::atomic<int> consumed_count{0};
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    for (int t = 0; t < kNumProducers; ++t)
    {
        producers.emplace_back([&q, t, &produced_count]
                               {
            for (int i = 0; i < kItemsPerProducer; ++i)
            {
                q.enqueue(t * kItemsPerProducer + i);
                produced_count.fetch_add(1);
            } });
    }

    for (int t = 0; t < kNumConsumers; ++t)
    {
        consumers.emplace_back([&q, &sum, &consumed_count]
                               {
            for (int i = 0; i < kItemsPerProducer; ++i)
            {
                sum.fetch_add(*q.dequeue());
                consumed_count.fetch_add(1);
            } });
    }

    for (auto &t : producers)
        t.join();
    for (auto &t : consumers)
        t.join();

    EXPECT_TRUE(q.empty());
    EXPECT_EQ(produced_count.load(), kNumProducers * kItemsPerProducer);
    EXPECT_EQ(consumed_count.load(), kNumProducers * kItemsPerProducer);

    int expected = 0;
    for (int t = 0; t < kNumProducers; ++t)
        for (int i = 0; i < kItemsPerProducer; ++i)
            expected += t * kItemsPerProducer + i;
    EXPECT_EQ(sum.load(), expected);
}

// ---- enqueue_overwrite from multiple producers ----
TEST(MpmcBlockingQueueTest, MpmcOverwriteStress)
{
    constexpr int kNumProducers = 4;
    constexpr int kItemsPerProducer = 500;

    mpmc_blocking_queue<int> q(32);

    std::vector<std::thread> producers;
    for (int t = 0; t < kNumProducers; ++t)
    {
        producers.emplace_back([&q, t]
                               {
            for (int i = 0; i < kItemsPerProducer; ++i)
                q.enqueue_overwrite(t * kItemsPerProducer + i); });
    }

    for (auto &t : producers)
        t.join();

    EXPECT_EQ(q.size(), 32);
    // Items should be drained without blocking
    while (!q.empty())
        q.dequeue();
}

// ---- single-element queue blocking ----
TEST(MpmcBlockingQueueTest, SingleElementQueue)
{
    mpmc_blocking_queue<int> q(1);

    q.enqueue(42);
    EXPECT_TRUE(q.full());

    std::thread consumer([&q]
                         { q.dequeue(); });

    // Producer should be able to enqueue after consumer drains the slot
    q.enqueue(43);

    consumer.join();
    EXPECT_EQ(*q.dequeue(), 43);
}

// ==================== 中断模式测试 ====================

// ---- enqueue returns bool ----
TEST(MpmcBlockingQueueTest, EnqueueReturnsBool)
{
    mpmc_blocking_queue<int> q(2);
    EXPECT_TRUE(q.enqueue(1));
    EXPECT_TRUE(q.enqueue(2));
}

// ---- enqueue returns false after stop_gracefully ----
TEST(MpmcBlockingQueueTest, EnqueueFailsAfterStopGracefully)
{
    mpmc_blocking_queue<int> q(2);
    q.stop_gracefully();
    EXPECT_FALSE(q.enqueue(1));
}

// ---- enqueue returns false after stop_immediately ----
TEST(MpmcBlockingQueueTest, EnqueueFailsAfterStopImmediately)
{
    mpmc_blocking_queue<int> q(2);
    q.stop_immediately();
    EXPECT_FALSE(q.enqueue(1));
}

// ---- enqueue_overwrite returns false after stop ----
TEST(MpmcBlockingQueueTest, EnqueueOverwriteFailsAfterStop)
{
    mpmc_blocking_queue<int> q(3);
    q.stop_gracefully();
    EXPECT_FALSE(q.enqueue_overwrite(1));
}

// ---- try_enqueue returns false after stop (full queue + stop) ----
TEST(MpmcBlockingQueueTest, TryEnqueueFailsAfterStop)
{
    mpmc_blocking_queue<int> q(2);
    q.enqueue(1);
    q.enqueue(2);
    q.stop_gracefully();
    EXPECT_FALSE(q.try_enqueue(3));
}

// ---- stop_gracefully: dequeue drains remaining items, then returns nullopt ----
TEST(MpmcBlockingQueueTest, StopGracefullyDrainsThenNullopt)
{
    mpmc_blocking_queue<int> q(4);

    q.enqueue(10);
    q.enqueue(20);
    q.enqueue(30);
    EXPECT_EQ(q.size(), 3);

    q.stop_gracefully();

    // 应排空已有数据
    auto v1 = q.dequeue();
    ASSERT_TRUE(v1.has_value());
    EXPECT_EQ(*v1, 10);

    auto v2 = q.dequeue();
    ASSERT_TRUE(v2.has_value());
    EXPECT_EQ(*v2, 20);

    auto v3 = q.dequeue();
    ASSERT_TRUE(v3.has_value());
    EXPECT_EQ(*v3, 30);

    // 排空后返回 nullopt
    auto v4 = q.dequeue();
    EXPECT_FALSE(v4.has_value());
}

// ---- stop_immediately: dequeue returns nullopt, skipping remaining items ----
TEST(MpmcBlockingQueueTest, StopImmediatelySkipsData)
{
    mpmc_blocking_queue<int> q(4);

    q.enqueue(10);
    q.enqueue(20);
    q.enqueue(30);

    q.stop_immediately();

    auto v = q.dequeue();
    EXPECT_FALSE(v.has_value()); // 立即返回 nullopt，不排空
}

// ---- stop_gracefully wakes blocked dequeue ----
TEST(MpmcBlockingQueueTest, StopGracefullyWakesBlockedDequeue)
{
    mpmc_blocking_queue<int> q(2);

    std::atomic<bool> woken{false};
    std::thread consumer([&q, &woken]
                         {
                             auto v = q.dequeue();
                             woken.store(!v.has_value()); // 应被中断唤醒，返回 nullopt
                         });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.stop_gracefully();

    consumer.join();
    EXPECT_TRUE(woken.load());
}

// ---- stop_immediately wakes blocked dequeue ----
TEST(MpmcBlockingQueueTest, StopImmediatelyWakesBlockedDequeue)
{
    mpmc_blocking_queue<int> q(2);

    std::atomic<bool> woken{false};
    std::thread consumer([&q, &woken]
                         {
        auto v = q.dequeue();
        woken.store(!v.has_value()); });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.stop_immediately();

    consumer.join();
    EXPECT_TRUE(woken.load());
}

// ---- stop wakes blocked enqueue ----
TEST(MpmcBlockingQueueTest, StopWakesBlockedEnqueue)
{
    mpmc_blocking_queue<int> q(1);

    q.enqueue(1); // full

    std::atomic<bool> woken{false};
    std::thread producer([&q, &woken]
                         {
        bool ok = q.enqueue(2); // blocks until stop
        woken.store(!ok); }); // should return false due to stop

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.stop_gracefully();

    producer.join();
    EXPECT_TRUE(woken.load());
}

// ---- dequeue_for with stop_gracefully drains then returns nullopt ----
TEST(MpmcBlockingQueueTest, DequeueForStopGracefully)
{
    mpmc_blocking_queue<int> q(3);

    q.enqueue(100);
    q.stop_gracefully();

    auto v = q.dequeue_for(std::chrono::milliseconds(500));
    ASSERT_TRUE(v.has_value());
    EXPECT_EQ(*v, 100);

    auto v2 = q.dequeue_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(v2.has_value());
}

// ---- dequeue_for with stop_immediately returns nullopt ----
TEST(MpmcBlockingQueueTest, DequeueForStopImmediately)
{
    mpmc_blocking_queue<int> q(3);

    q.enqueue(100);
    q.enqueue(200);
    q.stop_immediately();

    auto v = q.dequeue_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(v.has_value());
}

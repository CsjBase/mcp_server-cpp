#include "utils/thread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using utils::ThreadPool;

// ---- constructor validation ----
TEST(ThreadPoolTest, ConstructorRejectsZeroThreads)
{
    EXPECT_THROW(ThreadPool(0, 4), std::invalid_argument);
}

TEST(ThreadPoolTest, ConstructorRejectsZeroQueueSize)
{
    EXPECT_THROW(ThreadPool(4, 0), std::invalid_argument);
}

TEST(ThreadPoolTest, ConstructorSucceeds)
{
    ThreadPool pool(2, 4);
    EXPECT_EQ(pool.pending_count(), 0);
    EXPECT_EQ(pool.completed_count(), 0);
}

// ---- basic submit / execute ----
TEST(ThreadPoolTest, SingleTask)
{
    ThreadPool pool(2, 4);

    std::atomic<int> result{0};
    pool.submit([&result]
                { result.store(42); });
    pool.stop_gracefully();

    EXPECT_EQ(result.load(), 42);
    EXPECT_EQ(pool.completed_count(), 1);
}

TEST(ThreadPoolTest, MultipleTasks)
{
    ThreadPool pool(4, 64);

    std::atomic<int> counter{0};
    constexpr int kTasks = 100;

    for (int i = 0; i < kTasks; ++i)
        pool.submit([&counter]
                    { counter.fetch_add(1); });

    pool.stop_gracefully();

    EXPECT_EQ(counter.load(), kTasks);
    EXPECT_EQ(pool.completed_count(), kTasks);
}

// ---- try_submit ----
TEST(ThreadPoolTest, TrySubmitSuccess)
{
    ThreadPool pool(2, 64);
    EXPECT_TRUE(pool.try_submit([]
                                {}));
    pool.stop_gracefully();
    EXPECT_EQ(pool.completed_count(), 1);
}

TEST(ThreadPoolTest, TrySubmitFailsWhenFull)
{
    ThreadPool pool(1, 1);

    // Fill the single slot
    pool.submit([]
                { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });

    // Queue should be full, try_submit should fail
    EXPECT_FALSE(pool.try_submit([]
                                 {}));

    pool.stop_gracefully();
}

// ---- submit_overwrite ----
TEST(ThreadPoolTest, SubmitOverwriteReplacesOldest)
{
    ThreadPool pool(1, 1);

    std::atomic<int> executed{0};

    // Fill queue (blocking submit)
    pool.submit([&executed]
                { executed.store(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); });

    // Overwrite
    EXPECT_TRUE(pool.submit_overwrite([&executed]
                                      { executed.store(2); }));

    pool.stop_gracefully();
    EXPECT_EQ(executed.load(), 2); // overwritten task should execute instead
}

// ---- pending_count / completed_count ----
TEST(ThreadPoolTest, PendingAndCompletedCounts)
{
    ThreadPool pool(2, 4);

    pool.submit([]
                { std::this_thread::sleep_for(std::chrono::milliseconds(30)); });
    pool.submit([]
                { std::this_thread::sleep_for(std::chrono::milliseconds(30)); });
    pool.submit([]
                {});

    // Tasks may still be pending or running
    EXPECT_LE(pool.pending_count(), 3);

    pool.stop_gracefully();

    EXPECT_EQ(pool.pending_count(), 0);
    EXPECT_EQ(pool.completed_count(), 3);
}

// ---- active_count ----
TEST(ThreadPoolTest, ActiveCountDuringExecution)
{
    ThreadPool pool(4, 8);

    std::atomic<int> max_active{0};

    for (int i = 0; i < 8; ++i)
    {
        pool.submit([&pool, &max_active]
                    {
            auto stats = pool.get_stats();
            int a = static_cast<int>(stats.active_threads);
            if (a > max_active.load()) max_active.store(a);
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    }

    pool.stop_gracefully();
    EXPECT_GE(max_active.load(), 2); // at least 2 workers should be active concurrently
}

// ---- stats ----
TEST(ThreadPoolTest, StatsReflectState)
{
    ThreadPool pool(4, 32);

    for (int i = 0; i < 10; ++i)
        pool.submit([]
                    {});

    pool.stop_gracefully();

    auto s = pool.get_stats();
    EXPECT_EQ(s.completed_tasks, 10);
    EXPECT_EQ(s.pending_tasks, 0);
    EXPECT_EQ(s.total_threads, 4);
}

TEST(ThreadPoolTest, ResetStats)
{
    ThreadPool pool(2, 8);

    for (int i = 0; i < 5; ++i)
        pool.submit([]
                    {});
    pool.stop_gracefully();

    EXPECT_EQ(pool.completed_count(), 5);

    pool.reset_stats();
    EXPECT_EQ(pool.completed_count(), 0);
}

// ---- stop_gracefully: drains remaining tasks ----
TEST(ThreadPoolTest, StopGracefullyDrainsPending)
{
    ThreadPool pool(2, 16);

    std::atomic<int> counter{0};
    for (int i = 0; i < 20; ++i)
        pool.submit([&counter]
                    { counter.fetch_add(1); });

    pool.stop_gracefully();

    EXPECT_EQ(counter.load(), 20);
    EXPECT_EQ(pool.pending_count(), 0);
    EXPECT_EQ(pool.completed_count(), 20);
}

// ---- stop_immediately: discards remaining tasks ----
TEST(ThreadPoolTest, StopImmediatelyDiscardsPending)
{
    ThreadPool pool(1, 8);

    std::atomic<int> counter{0};

    // Submit a slow task first, then more tasks
    pool.submit([&counter]
                { std::this_thread::sleep_for(std::chrono::milliseconds(50));
        counter.fetch_add(1); });
    for (int i = 0; i < 5; ++i)
        pool.submit([&counter]
                    { counter.fetch_add(1); });

    pool.stop_immediately();

    // Only the first task + possibly some that already started should execute
    // The rest are discarded
    EXPECT_LT(counter.load(), 6);
}

// ---- destructor stops gracefully ----
TEST(ThreadPoolTest, DestructorStopsGracefully)
{
    std::atomic<int> counter{0};

    {
        ThreadPool pool(2, 8);
        for (int i = 0; i < 10; ++i)
            pool.submit([&counter]
                        { counter.fetch_add(1); });
    } // destructor should drain

    EXPECT_EQ(counter.load(), 10);
}

// ---- tasks with arguments ----
TEST(ThreadPoolTest, SubmitWithArgs)
{
    ThreadPool pool(2, 4);

    std::atomic<int> sum{0};
    pool.submit([](std::atomic<int> &s, int a, int b)
                { s.store(a + b); },
                std::ref(sum), 3, 4);
    pool.stop_gracefully();

    EXPECT_EQ(sum.load(), 7);
}

// ---- shared_ptr task (std::function requires copyable callable) ----
TEST(ThreadPoolTest, SubmitWithSharedPtr)
{
    ThreadPool pool(2, 4);

    auto ptr = std::make_shared<int>(99);
    pool.submit([ptr]
                { EXPECT_EQ(*ptr, 99); });
    pool.stop_gracefully();
}

// ---- multi-producer stress ----
TEST(ThreadPoolTest, MultiProducerStress)
{
    ThreadPool pool(8, 64);

    std::atomic<int> sum{0};
    constexpr int kProducers = 4;
    constexpr int kItemsPerProducer = 250;

    std::vector<std::thread> producers;
    for (int t = 0; t < kProducers; ++t)
    {
        producers.emplace_back([&pool, &sum, t]
                               {
            for (int i = 0; i < kItemsPerProducer; ++i)
                pool.submit([&sum, v = t * kItemsPerProducer + i]
                            { sum.fetch_add(v); }); });
    }

    for (auto &t : producers)
        t.join();

    pool.stop_gracefully();

    int expected = 0;
    for (int t = 0; t < kProducers; ++t)
        for (int i = 0; i < kItemsPerProducer; ++i)
            expected += t * kItemsPerProducer + i;
    EXPECT_EQ(sum.load(), expected);
    EXPECT_EQ(pool.completed_count(), kProducers * kItemsPerProducer);
}

#include "utils/circular_q.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

using utils::circular_q;

// ---- default construction ----
TEST(CircularQTest, DefaultCtor)
{
    circular_q<int> q;
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0);
    EXPECT_EQ(q.overrun_counter(), 0);

    q.push_back(42);
    EXPECT_TRUE(q.empty());
}

// ---- sized construction ----
TEST(CircularQTest, SizedCtor)
{
    circular_q<int> q(4);
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.full());
    EXPECT_EQ(q.size(), 0);
}

// ---- basic push / front / pop ----
TEST(CircularQTest, PushFrontPop)
{
    circular_q<int> q(4);

    q.push_back(10);
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1);
    EXPECT_EQ(q.front(), 10);

    q.push_back(20);
    q.push_back(30);
    EXPECT_EQ(q.size(), 3);
    EXPECT_EQ(q.front(), 10);

    q.pop_front();
    EXPECT_EQ(q.size(), 2);
    EXPECT_EQ(q.front(), 20);

    q.pop_front();
    EXPECT_EQ(q.front(), 30);
    q.pop_front();
    EXPECT_TRUE(q.empty());
}

// ---- full / overrun ----
TEST(CircularQTest, FullAndOverrun)
{
    circular_q<int> q(3);

    q.push_back(1);
    q.push_back(2);
    q.push_back(3);
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 3);
    EXPECT_EQ(q.overrun_counter(), 0);

    q.push_back(4);
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 3);
    EXPECT_EQ(q.front(), 2);
    EXPECT_EQ(q.overrun_counter(), 1);

    q.push_back(5);
    EXPECT_EQ(q.front(), 3);
    EXPECT_EQ(q.overrun_counter(), 2);

    q.reset_overrun_counter();
    EXPECT_EQ(q.overrun_counter(), 0);
}

// ---- string type ----
TEST(CircularQTest, StringType)
{
    circular_q<std::string> q(2);

    q.push_back(std::string("hello"));
    q.push_back(std::string("world"));

    EXPECT_EQ(q.front(), "hello");
    q.pop_front();
    EXPECT_EQ(q.front(), "world");
}

// ---- move-only type (std::unique_ptr) ----
TEST(CircularQTest, UniquePtrType)
{
    circular_q<std::unique_ptr<int>> q(3);
    q.push_back(std::make_unique<int>(100));
    q.push_back(std::make_unique<int>(200));

    EXPECT_EQ(*q.front(), 100);
    q.pop_front();
    EXPECT_EQ(*q.front(), 200);
}

// ---- non-const front for mutation ----
TEST(CircularQTest, MutableFront)
{
    circular_q<int> q(3);
    q.push_back(5);
    q.front() = 99;
    EXPECT_EQ(q.front(), 99);
}

// ---- circular wrap-around ----
TEST(CircularQTest, WrapAround)
{
    circular_q<int> q(4);

    for (int round = 0; round < 5; ++round)
    {
        for (int i = 0; i < 4; ++i)
            q.push_back(round * 10 + i);

        EXPECT_TRUE(q.full());
        EXPECT_EQ(q.front(), round * 10);

        for (int i = 0; i < 4; ++i)
            q.pop_front();

        EXPECT_TRUE(q.empty());
    }
}

// ---- single element queue ----
TEST(CircularQTest, SingleElement)
{
    circular_q<int> q(1);
    EXPECT_FALSE(q.full());

    q.push_back(42);
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.front(), 42);

    q.push_back(99);
    EXPECT_EQ(q.front(), 99);
    EXPECT_EQ(q.overrun_counter(), 1);
}

// ---- size across wrap boundary ----
TEST(CircularQTest, SizeAcrossWrap)
{
    circular_q<int> q(5);

    q.push_back(0);
    q.push_back(1);
    q.push_back(2);
    q.pop_front();
    q.pop_front();

    EXPECT_EQ(q.size(), 1);

    q.push_back(10);
    q.push_back(11);
    q.push_back(12);
    q.push_back(13);

    EXPECT_EQ(q.size(), 5);
    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.front(), 2);
}

// ---- copy constructor ----
TEST(CircularQTest, CopyCtor)
{
    circular_q<int> q(3);
    q.push_back(10);
    q.push_back(20);

    circular_q<int> q2(q);
    EXPECT_EQ(q2.size(), 2);
    EXPECT_EQ(q2.front(), 10);

    q2.pop_front();
    EXPECT_EQ(q2.front(), 20);
    EXPECT_EQ(q.front(), 10);
}

// ---- copy assignment ----
TEST(CircularQTest, CopyAssignment)
{
    circular_q<int> q(3);
    q.push_back(10);
    q.push_back(20);

    circular_q<int> q2(5);
    q2.push_back(99);
    q2 = q;

    EXPECT_EQ(q2.size(), 2);
    EXPECT_EQ(q2.front(), 10);

    q2.pop_front();
    EXPECT_EQ(q2.front(), 20);
    EXPECT_EQ(q.front(), 10);
}

// ---- stress: push/pop repeatedly ----
TEST(CircularQTest, StressPushPop)
{
    circular_q<int> q(64);

    for (int i = 0; i < 10000; ++i)
    {
        q.push_back(int(i));
        if (q.full())
            q.pop_front();
    }

    // Queue hovers at capacity-1 since we pop immediately when full
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 63);
    EXPECT_EQ(q.overrun_counter(), 0);
}

// ---- fill exactly to capacity and drain completely ----
TEST(CircularQTest, FillAndDrain)
{
    circular_q<int> q(8);

    for (int i = 0; i < 8; ++i)
        q.push_back(int(i));

    EXPECT_TRUE(q.full());
    EXPECT_EQ(q.size(), 8);

    for (int i = 0; i < 8; ++i)
    {
        EXPECT_EQ(q.front(), i);
        q.pop_front();
    }

    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0);
}

#pragma once

#include "net/EventLoopThread.h"

namespace net
{
    class EventLoopThreadPool
    {
    public:
        using ThreadInitCallback = std::function<void(EventLoop *)>;

        EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg);
        ~EventLoopThreadPool();

        EventLoopThreadPool(const EventLoopThreadPool &) = delete;
        EventLoopThreadPool &operator=(const EventLoopThreadPool &) = delete;

        void setThreadNum(int numThreads) { m_numThreads = numThreads; }

        void start(const ThreadInitCallback &cb = ThreadInitCallback());

        // 如果工作在多线程中，baseLoop_默认以轮询的方式分配channel给subloop
        EventLoop *getNextLoop();

        std::vector<EventLoop *> getAllLoop();

        bool started() const { return m_started; }

        const std::string name() const { return m_name; }

    private:
        EventLoop *m_baseLoop;
        std::string m_name;
        bool m_started;
        int m_numThreads;
        size_t m_next;
        std::vector<std::unique_ptr<EventLoopThread>> m_threads;
        std::vector<EventLoop *> m_loops;
    };
}
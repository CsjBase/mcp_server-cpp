#pragma once

#include "net/base/Thread.h"

#include <condition_variable>

namespace net
{
    class EventLoop;
    class EventLoopThread
    {
    public:
        using ThreadInitCallback = std::function<void(EventLoop *)>;

        EventLoopThread(const std::string &name = std::string(), const ThreadInitCallback &cb = ThreadInitCallback());
        ~EventLoopThread();

        EventLoop *startLoop();

    private:
        void threadFunc();

    private:
        std::mutex m_mutex;
        std::condition_variable m_cond;
        EventLoop *m_loop;
        bool m_exiting;
        Thread m_thread;
        ThreadInitCallback m_callback;
    };
}
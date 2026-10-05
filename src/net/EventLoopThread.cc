#include "net/EventLoopThread.h"
#include "net/EventLoop.h"
#include "net/base/Log.h"

namespace net
{
    EventLoopThread::EventLoopThread(const std::string &name, const ThreadInitCallback &cb)
        : m_loop(nullptr),
          m_exiting(false),
          m_thread(std::bind(&EventLoopThread::threadFunc, this), name),
          m_callback(cb)
    {
    }

    EventLoopThread::~EventLoopThread()
    {
        m_exiting = true;
        if (m_loop != nullptr)
        {
            m_loop->quit();
            m_thread.join();
        }
    }

    EventLoop *EventLoopThread::startLoop()
    {
        m_thread.start(); // 启动底层的新线程

        m_loop = m_promise.get_future().get();
        return m_loop;

        // EventLoop *loop = nullptr;
        // {
        //     std::unique_lock lock(m_mutex);
        //     while (m_loop == nullptr)
        //     {
        //         m_cond.wait(lock);
        //     }
        //     loop = m_loop;
        // }
        // return loop;
    }

    void EventLoopThread::threadFunc()
    {
        NET_LOG_DEBUG("{} start.", m_thread.name());
        EventLoop loop; // 创建一个独立的eventLoop，和上面的线程是一一对应的，one loop per pthread

        if (m_callback)
        {
            m_callback(&loop);
        }

        m_promise.set_value(&loop);
        // {
        //     std::lock_guard lock(m_mutex);
        //     m_loop = &loop;
        //     m_cond.notify_all();
        // }
        loop.loop(); // EventLoop loop => Poller.poll
        m_loop = nullptr;
    }
}
#pragma once

#include "net/Channel.h"
#include "net/Poller.h"
#include "utils/os.h"
#include "net/TimerManager.h"

#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

namespace net
{
    class EventLoop
    {
    public:
        typedef std::unique_ptr<EventLoop> ptr;
        typedef std::function<void()> Functor;

        EventLoop();
        ~EventLoop();
        EventLoop(const EventLoop &) = delete;
        EventLoop &operator=(const EventLoop &) = delete;

        // 开启事件循环
        void loop();
        // 结束事件循环
        void quit();

        Timestamp pollReturnTime() const { return m_pollReturnTime; }

        // 在当前Loop中执行
        void runInLoop(Functor cb);
        // 把cb放入队列中，唤醒loop所在的线程，执行cb
        void queueInLoop(Functor cb);

        ///
        /// Runs callback at 'time'.
        /// Safe to call from other threads.
        ///
        TimerId runAt(Timestamp time, TimerCallback cb);
        ///
        /// Runs callback after @c delay seconds.
        /// Safe to call from other threads.
        ///
        TimerId runAfter(double delay, TimerCallback cb);
        ///
        /// Runs callback every @c interval seconds.
        /// Safe to call from other threads.
        ///
        TimerId runEvery(double interval, TimerCallback cb);
        ///
        /// Cancels the timer.
        /// Safe to call from other threads.
        ///
        void cancel(TimerId timerId);

        // 用来唤醒loop所在线程的
        void wakeup();

        // EventLoop的方法 =》 Poller的方法
        void updateChannel(Channel *channel);
        void removeChannel(Channel *channel);
        bool hasChannel(Channel *channel);

        void assertInLoopThread()
        {
            if (!isInLoopThread())
            {
                abortNotInLoopThread();
            }
        }

        // 判断EventLoop对象是否在自己的线程里面
        bool isInLoopThread() const { return m_threadId == utils::thread_id(); }

    private:
        void handleRead();        // wake up
        void doPendingFunctors(); // 执行回调

        void abortNotInLoopThread();

        typedef std::vector<Channel *> ChannelList;

        std::atomic_bool m_looping;
        std::atomic_bool m_quit;
        const pid_t m_threadId;     // 基类当前loop所在线程的id
        Timestamp m_pollReturnTime; // poller返回发生事件的channels的时间点
        Poller *m_poller;
        std::unique_ptr<TimerManager> m_timerManager;

        int m_wakeupFd; // 主要作用：当mainLoop获取新用户的channel，通过轮询算法选择一个subLoop，通过该成员唤醒subLoop处理channel
        std::unique_ptr<Channel> m_wakeupChannel;

        ChannelList m_activeChannels;

        std::atomic_bool m_callingPendingFunctors; // 标识当前线程释放有需要执行的回调操作
        std::vector<Functor> m_pendingFunctors;    // 存储loop需要执行的所有回调操作
        std::mutex m_mutex;                        // 用来保护上面vector容器的线程安全操作
    };

}

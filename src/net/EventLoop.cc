#include "net/EventLoop.h"
#include "logger/log.h"
#include "net/base/SocketOps.h"

#include <sys/eventfd.h>
#include <assert.h>

namespace net
{

    __thread EventLoop *t_loopInThisThread = 0;

    EventLoop::EventLoop()
        : m_looping(false), m_quit(false), m_threadId(utils::thread_id()),
          m_poller(Poller::newDefaultPoller(this)), m_wakeupFd(createEventfd()),
          m_wakeupChannel(new Channel(this, m_wakeupFd)), m_callingPendingFunctors(false)
    {
        if (t_loopInThisThread)
        {
            LOG_FATAL(LOGGER_DEFAULT(), "Another EventLoop {} exists in this thread", (void *)t_loopInThisThread);
        }
        else
        {
            t_loopInThisThread = this;
        }

        // 每个EventLoop都将监听wakeupChannel的EPOLLIN读事件
        m_wakeupChannel->enableReading();
        // 设置wakeupfd的事件类型以及发生事件后的回调操作
        m_wakeupChannel->setReadCallback(std::bind(&EventLoop::handleRead, this));
    }

    EventLoop::~EventLoop()
    {
        assertInLoopThread();
        m_wakeupChannel->disableAll();
        m_wakeupChannel->remove();
        ::close(m_wakeupFd);
        t_loopInThisThread = nullptr;
    }

    const int kPollTimeMs = 2000;

    void EventLoop::loop()
    {
        assertInLoopThread();

        m_looping = true;
        m_quit = false;

        LOG_DEBUG(LOGGER_DEFAULT(), "EventLoop {} start looping.", (void *)this);

        while (!m_quit)
        {
            m_activeChannels.clear();
            // 监听两类fd 一类是clientfd，另一种是wakeupfd
            m_pollReturnTime = m_poller->poll(kPollTimeMs, &m_activeChannels);
            for (Channel *channel : m_activeChannels)
            {
                // Poller监听哪些channel发生事件了，然后上报给EventLoop，通知channel处理相应的事件
                channel->handleEvent(m_pollReturnTime);
            }

            // 执行当前EventLoop事件循环需要处理的回调操作
            /**
             * IO线程 mainloop accept fd 打包 =》 channel 分发给 subloop
             * mainLoop事先注册一个回调cb（需要subLoop来执行） wakeup subLoop后，执行下面的方法，执行之前mainLoop注册的cb操作
             */
            doPendingFunctors();
        }
        LOG_DEBUG(LOGGER_DEFAULT(), "EventLoop {} stop looping.", (void *)this);
    }

    void EventLoop::quit()
    {
        m_quit = true;

        if (!isInLoopThread())
        {
            wakeup();
        }
    }

    void EventLoop::runInLoop(Functor cb)
    {
        if (isInLoopThread())
        { // 在当前loop线程中执行cb
            cb();
        }
        else
        { // 在非当前loop线程中执行cb，需要唤醒loop所在线程，执行cb
            queueInLoop(cb);
        }
    }

    void EventLoop::queueInLoop(Functor cb)
    {
        {
            std::lock_guard lock(m_mutex);
            m_pendingFunctors.emplace_back(cb);
        }

        // 唤醒相应的，需要执行上面回调操作的loop线程
        //  ||m_callingPendingFunctors表示当前loop正在执行回调，但是loop又有了新的回调，为了防止有回调未处理时阻塞等待
        if (!isInLoopThread() || m_callingPendingFunctors)
        { // callingPendingFunctors_待解释
            wakeup();
        }
    }

    void EventLoop::wakeup()
    {
        uint64_t one = 1;
        ssize_t n = write(m_wakeupFd, &one, sizeof one);
        if (n != sizeof one)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "EventLoop::wakeup() write {} bytes instead of 8", n);
        }
    }

    void EventLoop::updateChannel(Channel *channel)
    {
        assert(channel->ownerLoop() == this);
        assertInLoopThread();
        m_poller->updateChannel(channel);
    }

    void EventLoop::removeChannel(Channel *channel)
    {
        assert(channel->ownerLoop() == this);
        assertInLoopThread();
        m_poller->removeChannel(channel);
    }

    bool EventLoop::hasChannel(Channel *channel)
    {
        assert(channel->ownerLoop() == this);
        assertInLoopThread();
        return m_poller->hasChannel(channel);
    }

    void EventLoop::handleRead()
    {
        uint64_t one = 1;
        ssize_t n = read(m_wakeupFd, &one, sizeof one);
        if (n != sizeof one)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "EventLoop::handleRead() reads {} bytes instead of 8", n);
        }
    }

    void EventLoop::doPendingFunctors()
    {
        std::vector<Functor> functors;
        m_callingPendingFunctors = true;

        {
            std::lock_guard lock(m_mutex);
            functors.swap(m_pendingFunctors);
        }

        for (const Functor functor : functors)
        {
            functor(); // 执行当前loop需要执行的回调操作
        }

        m_callingPendingFunctors = false;
    }

    void EventLoop::abortNotInLoopThread()
    {
        LOG_FATAL(LOGGER_DEFAULT(), "m_threadId = {} current threadid = {}", m_threadId, utils::thread_id());
    }

}
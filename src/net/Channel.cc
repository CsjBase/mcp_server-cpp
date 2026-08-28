#include "net/Channel.h"
#include "net/EventLoop.h"

namespace net
{
    const int Channel::kNoneEvent = 0;
    const int Channel::kReadEvent = POLLIN | POLLPRI;
    const int Channel::kWriteEvent = POLLOUT;

    Channel::Channel(EventLoop *loop, int fd)
        : m_loop(loop),
          m_fd(fd),
          m_events(0),
          m_revents(0),
          m_index(-1),
          m_tied(false)
    {
    }

    Channel::~Channel()
    {
    }

    void Channel::handleEvent(Timestamp receiveTime)
    {
        if (m_tied)
        {
            std::shared_ptr<void> guard = m_tie.lock();
            if (guard)
            {
                handleEventWithGuard(receiveTime);
            }
        }
        else
        {
            handleEventWithGuard(receiveTime);
        }
    }

    void Channel::tie(const std::shared_ptr<void> &obj)
    {
        m_tie = obj;
        m_tied = true;
    }

    void Channel::remove()
    {
        m_loop->removeChannel(this);
    }

    void Channel::update()
    {
        m_loop->updateChannel(this);
    }

    void Channel::handleEventWithGuard(Timestamp receiveTime)
    {
        if ((m_revents & POLLHUP) && !(m_revents & POLLIN))
        {
            if (m_closeCallback)
            {
                m_closeCallback();
            }
        }

        if (m_revents & POLLERR)
        {
            if (m_errorCallback)
            {
                m_errorCallback();
            }
        }

        if (m_revents & kReadEvent)
        {
            if (m_readCallback)
            {
                m_readCallback(receiveTime);
            }
        }

        if (m_revents & kWriteEvent)
        {
            if (m_writeCallback)
            {
                m_writeCallback();
            }
        }
    }

}
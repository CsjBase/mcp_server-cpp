#include "net/Poller.h"
#include "net/EventLoop.h"
#include "net/base/Log.h"

namespace net
{

    Poller::Poller(EventLoop *loop)
        : m_ownerLoop(loop)
    {
    }

    bool Poller::hasChannel(Channel *channel) const
    {
        assertInLoopThread();
        auto it = m_channels.find(channel->fd());
        return it != m_channels.end() && it->second == channel;
    }

    void Poller::assertInLoopThread() const
    {
        m_ownerLoop->assertInLoopThread();
    }

    Poller *Poller::newDefaultPoller(EventLoop *loop)
    {
        return new DefaultPoller(loop);
    }

    EPollPoller::EPollPoller(EventLoop *loop)
        : Poller(loop), m_epollfd(::epoll_create1(EPOLL_CLOEXEC)), m_events(kInitEventListSize)
    {
        if (m_epollfd < 0)
        {
            LOG_FATAL("epoll_create error! errno:{} errstr:{}", errno, strerror(errno));
        }
    }

    EPollPoller::~EPollPoller()
    {
        ::close(m_epollfd);
    }

    Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
    {
        int numEvents = ::epoll_wait(m_epollfd, &*m_events.begin(), static_cast<int>(m_events.size()), timeoutMs);
        int saveErrno = errno;
        Timestamp now(Timestamp::now());

        if (numEvents > 0)
        {
            fillActiveChannels(numEvents, activeChannels);
            if ((size_t)numEvents == m_events.size())
            {
                m_events.resize(m_events.size() * 2);
            }
        }
        else if (numEvents == 0)
        {
            // CSJ_LOG_ERROR(g_logger) << "EPollPoller::poll timeout. nothing happened";
        }
        else
        {
            if (saveErrno != EINTR)
            {
                errno = saveErrno;
                LOG_ERROR("EPollPoller::poll() err! errno:{} errstr:{}", saveErrno, strerror(saveErrno));
            }
        }
        return now;
    }

    void EPollPoller::updateChannel(Channel *channel)
    {
        Poller::assertInLoopThread();
        const int index = channel->index();
        int fd = channel->fd();

        if (Channel::kNew == index || Channel::kDeleted == index)
        {
            if (index == Channel::kNew)
            {
                m_channels[fd] = channel;
            }

            channel->setIndex(Channel::kAdded);
            update(EPOLL_CTL_ADD, channel);
        }
        else
        { // 已经在poller上注册过了
            if (channel->isNoneEvent())
            {
                update(EPOLL_CTL_DEL, channel);
                channel->setIndex(Channel::kDeleted);
            }
            else
            {
                update(EPOLL_CTL_MOD, channel);
            }
        }
    }

    void EPollPoller::removeChannel(Channel *channel)
    {
        Poller::assertInLoopThread();
        int fd = channel->fd();
        m_channels.erase(fd);

        int index = channel->index();
        if (index == Channel::kAdded)
        {
            update(EPOLL_CTL_DEL, channel);
        }

        channel->setIndex(Channel::kNew);
    }

    void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
    {
        for (int i = 0; i < numEvents; i++)
        {
            Channel *channel = static_cast<Channel *>(m_events[i].data.ptr);
            channel->set_revents(m_events[i].events);
            activeChannels->push_back(channel);
            // EventLoop就拿到了它的poller给他返回的所有发生事件的channel列表了
        }
    }

    void EPollPoller::update(int operation, Channel *channel)
    {
        epoll_event event;
        bzero(&event, sizeof event);

        int fd = channel->fd();
        event.events = channel->events();
        event.data.ptr = channel;

        if (::epoll_ctl(m_epollfd, operation, fd, &event) < 0)
        {
            if (operation == EPOLL_CTL_DEL)
            {
                LOG_ERROR("epoll_ctl del error! errno:{} errstr:", errno, strerror(errno));
            }
            else
            {
                LOG_ERROR("epoll_ctl add/modify error! errno:{} errstr:", errno, strerror(errno));
            }
        }
    }

    PollPoller::PollPoller(EventLoop *loop)
        : Poller(loop)
    {
    }

    PollPoller::~PollPoller()
    {
    }

    Timestamp PollPoller::poll(int timeoutMs, ChannelList *activeChannels)
    {
        int numEvents = ::poll(&*m_pollfds.begin(), m_pollfds.size(), timeoutMs);
        int saveErrno = errno;
        Timestamp now(Timestamp::now());
        if (numEvents > 0)
        {
            fillActiveChannels(numEvents, activeChannels);
        }
        else if (numEvents == 0)
        {
            LOG_DEBUG("PollPoller::poll timeout. nothing happened");
        }
        else
        {
            if (saveErrno != EINTR)
            {
                errno = saveErrno;
                LOG_ERROR("PollPoller::poll() err! errno::{} errstr:", saveErrno, strerror(saveErrno));
            }
        }
        return now;
    }

    void PollPoller::updateChannel(Channel *channel)
    {
        Poller::assertInLoopThread();
        if (channel->index() < 0)
        {
            struct pollfd pfd;
            pfd.fd = channel->fd();
            pfd.events = static_cast<short>(channel->events());
            pfd.revents = 0;
            m_pollfds.push_back(pfd);
            int idx = static_cast<int>(m_pollfds.size()) - 1;
            channel->setIndex(idx);
            m_channels[pfd.fd] = channel;
        }
        else
        {
            int idx = channel->index();
            struct pollfd &pfd = m_pollfds[idx];
            pfd.fd = channel->fd();
            pfd.events = static_cast<short>(channel->events());
            pfd.revents = 0;
            if (channel->isNoneEvent())
            {
                pfd.fd = -channel->fd() - 1;
            }
        }
    }

    void PollPoller::removeChannel(Channel *channel)
    {
        Poller::assertInLoopThread();
        int idx = channel->index();
        const struct pollfd &pfd = m_pollfds[idx];
        (void)pfd;
        size_t n = m_channels.erase(channel->fd());
        if (n != 1)
            return;
        if ((size_t)(idx) == m_pollfds.size() - 1)
        {
            m_pollfds.pop_back();
        }
        else
        {
            int channelAtEnd = m_pollfds.back().fd;
            iter_swap(m_pollfds.begin() + idx, m_pollfds.end() - 1);
            if (channelAtEnd < 0)
            {
                channelAtEnd = -channelAtEnd - 1;
            }
            m_channels[channelAtEnd]->setIndex(idx);
            m_pollfds.pop_back();
        }
    }

    void PollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
    {
        for (PollFdList::const_iterator pfd = m_pollfds.begin();
             pfd != m_pollfds.end() && numEvents > 0; ++pfd)
        {
            if (pfd->revents > 0)
            {
                --numEvents;
                ChannelMap::const_iterator ch = m_channels.find(pfd->fd);
                Channel *channel = ch->second;
                channel->set_revents(pfd->revents);
                // pfd->revents = 0;
                activeChannels->push_back(channel);
            }
        }
    }

    SelectPoller::SelectPoller(EventLoop *loop)
        : Poller(loop)
    {
        // 初始化文件描述符集合
        FD_ZERO(&m_readfds);
        FD_ZERO(&m_writefds);
    }

    SelectPoller::~SelectPoller()
    {
    }

    Timestamp SelectPoller::poll(int timeoutMs, ChannelList *activeChannels)
    {
        fd_set temp_readfds = m_readfds;
        fd_set temp_writefds = m_writefds;
        struct timeval timeout;            // 设置超时时间
        timeout.tv_sec = timeoutMs / 1000; // 设置超时时间
        timeout.tv_usec = (timeoutMs - timeoutMs / 1000 * 1000) * 1000;
        int numEvents = select(m_channels.size() + 1, &temp_readfds, &temp_writefds, NULL, &timeout);
        int saveErrno = errno;
        Timestamp now(Timestamp::now());
        if (numEvents > 0)
        {
            fillActiveChannels(numEvents, activeChannels, temp_readfds, temp_writefds);
        }
        else if (numEvents == 0)
        {
            // CSJ_LOG_ERROR(g_logger) << "SelectPoller::poll timeout. nothing happened";
        }
        else
        {
            if (saveErrno != EINTR)
            {
                errno = saveErrno;
                LOG_ERROR("SelectPoller::poll() err! errno:{} errstr:{}", saveErrno, strerror(saveErrno));
            }
        }
        return now;
    }

    void SelectPoller::updateChannel(Channel *channel)
    {
        Poller::assertInLoopThread();
        const int index = channel->index();
        int fd = channel->fd();
        if (Channel::kNew == index || Channel::kDeleted == index)
        {
            if (index == Channel::kNew)
            {
                m_channels[fd] = channel;
            }

            channel->setIndex(Channel::kAdded);
            update(EPOLL_CTL_ADD, channel);
        }
        else
        { // 已经在poller上注册过了
            if (channel->isNoneEvent())
            {
                update(EPOLL_CTL_DEL, channel);
                channel->setIndex(Channel::kDeleted);
            }
            else
            {
                update(EPOLL_CTL_MOD, channel);
            }
        }
    }

    void SelectPoller::removeChannel(Channel *channel)
    {
        Poller::assertInLoopThread();
        int fd = channel->fd();
        m_channels.erase(fd);

        int index = channel->index();
        if (index == Channel::kAdded)
        {
            update(EPOLL_CTL_DEL, channel);
        }

        channel->setIndex(Channel::kNew);
    }

    void SelectPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels, fd_set &readfds, fd_set &writefds) const
    {
        Channel *channel = NULL;
        bool eventTriggered = false;
        // TODO：每次遍历channels_，效率太低，能否改进？
        int currentCount = 0;
        for (const auto &iter : m_channels)
        {
            channel = iter.second;

            if (FD_ISSET(iter.first, &readfds))
            {
                channel->set_revents(EPOLLIN | EPOLLPRI);
                eventTriggered = true;
            }

            if (FD_ISSET(iter.first, &writefds))
            {
                channel->set_revents(EPOLLOUT);
                eventTriggered = true;
            }

            if (eventTriggered)
            {
                activeChannels->push_back(channel);
                // 重置标志
                eventTriggered = false;

                ++currentCount;
                if (currentCount >= numEvents)
                    break;
            }
        } // end for-loop
    }

    void SelectPoller::update(int operation, Channel *channel)
    {
        int fd = channel->fd();
        int events = channel->events();
        if (operation == EPOLL_CTL_ADD)
        {
            if (events & EPOLLIN && !FD_ISSET(fd, &m_readfds))
            { // 新增读事件
                FD_SET(fd, &m_readfds);
            }

            if (events & EPOLLOUT && !FD_ISSET(fd, &m_readfds))
            { // 新增写事件
                FD_SET(fd, &m_writefds);
            }
        }
        else if (operation == EPOLL_CTL_DEL)
        { // 移除读写事件
            FD_CLR(fd, &m_readfds);
            FD_CLR(fd, &m_writefds);
        }
        else if (operation == EPOLL_CTL_MOD)
        {
            if (events & EPOLLIN && !FD_ISSET(fd, &m_readfds))
            { // 新增读事件
                FD_SET(fd, &m_readfds);
            }
            else if (!(events & EPOLLIN) && FD_ISSET(fd, &m_readfds))
            { // 移除读事件
                FD_CLR(fd, &m_readfds);
            }

            if (events & EPOLLOUT && !FD_ISSET(fd, &m_writefds))
            { // 新增写事件
                FD_SET(fd, &m_writefds);
            }
            else if (!(events & EPOLLIN) && FD_ISSET(fd, &m_writefds))
            { // 移除写事件
                FD_CLR(fd, &m_writefds);
            }
        }
    }

}
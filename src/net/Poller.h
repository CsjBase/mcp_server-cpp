#pragma once

#include "net/base/Timestamp.h"
#include "net/Channel.h"

#include <vector>
#include <unordered_map>
#include <sys/epoll.h>
#include <poll.h>
#include <map>

namespace net
{

    class EPollPoller;
    class EventLoop;
    using DefaultPoller = EPollPoller;

    class Poller
    {
    public:
        typedef std::vector<Channel *> ChannelList;

        Poller(EventLoop *loop);

        Poller(const Poller &) = delete;
        Poller &operator=(const Poller &) = delete;

        virtual ~Poller() = default;
        // 给所用IO复用保留统一的接口
        virtual Timestamp poll(int timeoutMs, ChannelList *activeChannel) = 0;
        virtual void updateChannel(Channel *channel) = 0;
        virtual void removeChannel(Channel *channel) = 0;
        bool hasChannel(Channel *channel) const;
        void assertInLoopThread() const;
        // EventLoop可以通过该接口获取默认的IO复用的具体实现
        static Poller *newDefaultPoller(EventLoop *loop);

    protected:
        // map的key表示sockfd value表示sockefd所属的channel通道
        using ChannelMap = std::unordered_map<int, Channel *>;
        ChannelMap m_channels;

    private:
        EventLoop *m_ownerLoop;
    };

    class EPollPoller : public Poller
    {
    public:
        EPollPoller(EventLoop *loop);
        ~EPollPoller() override;

        // 重写基类的Poller抽象方法
        Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
        void updateChannel(Channel *channel) override;
        void removeChannel(Channel *channel) override;

    private:
        static const int kInitEventListSize = 16;

        void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
        void update(int operation, Channel *channel);

        typedef std::vector<epoll_event> EventList;

        int m_epollfd;
        EventList m_events;
    };

    class PollPoller : public Poller
    {
    public:
        PollPoller(EventLoop *loop);
        ~PollPoller() override;

        Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
        void updateChannel(Channel *channel) override;
        void removeChannel(Channel *channel) override;

    private:
        void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;

        typedef std::vector<struct pollfd> PollFdList;
        PollFdList m_pollfds;
    };

    class SelectPoller : public Poller
    {
    public:
        SelectPoller(EventLoop *loop);
        ~SelectPoller() override;

        Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
        void updateChannel(Channel *channel) override;
        void removeChannel(Channel *channel) override;

    private:
        void fillActiveChannels(int numEvents, ChannelList *activeChannels, fd_set &readfds, fd_set &writefds) const;
        void update(int operation, Channel *channel);

    private:
        using ChannelMap = std::map<int, Channel *>;

        ChannelMap m_channels;
        fd_set m_readfds;
        fd_set m_writefds;
    };

}
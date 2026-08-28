#pragma once

#include "net/base/Address.h"
#include "net/base/Socket.h"
#include "net/Channel.h"
#include "EventLoop.h"

namespace net
{

    class Acceptor
    {
    public:
        using NewConnectionCallback = std::function<void(int sockfd, Address::ptr peerAddr)>;

        Acceptor(EventLoop *loop, Address::ptr listenAddr, bool reuseport);
        ~Acceptor();
        Acceptor(const Acceptor &) = delete;
        Acceptor &operator=(const Acceptor &) = delete;

        void setNewConnectionCallback(const NewConnectionCallback &cb)
        {
            m_newConnectionCallback = cb;
        }

        bool listenning() const { return m_listenning; }
        void listen();

    private:
        void handleRead();

    private:
        EventLoop *m_loop;
        Socket m_acceptSocket;
        Channel m_acceptChannel;
        NewConnectionCallback m_newConnectionCallback;
        bool m_listenning;
    };

}

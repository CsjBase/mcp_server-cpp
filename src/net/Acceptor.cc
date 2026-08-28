#include "net/Acceptor.h"
#include "net/base/SocketOps.h"
#include "logger/log.h"

namespace net
{
    Acceptor::Acceptor(EventLoop *loop, Address::ptr listenAddr, bool reuseport)
        : m_loop(loop), m_acceptSocket(createNonblockingOrDie(AF_INET)), m_acceptChannel(loop, m_acceptSocket.fd()), m_listenning(false)
    {
        m_acceptSocket.setReuseAddr(true);
        m_acceptSocket.setReusePort(reuseport);
        m_acceptSocket.bind_address(listenAddr); // bind
        m_acceptChannel.setReadCallback(std::bind(&Acceptor::handleRead, this));
    }

    Acceptor::~Acceptor()
    {
        m_acceptChannel.disableAll();
        m_acceptChannel.remove();
    }

    void Acceptor::listen()
    {
        m_loop->assertInLoopThread();

        m_listenning = true;
        m_acceptSocket.listen();         // listen
        m_acceptChannel.enableReading(); // m_acceptChannel => Poller
    }

    void Acceptor::handleRead()
    {
        m_loop->assertInLoopThread();

        Address::ptr peerAddr;
        int connfd = m_acceptSocket.accept(peerAddr);
        if (connfd >= 0)
        {
            if (m_newConnectionCallback)
            {
                m_newConnectionCallback(connfd, peerAddr); // 轮询找到subLoop，唤醒，分发当前的新客户端的Channel
            }
            else
            {
                ::close(connfd);
            }
        }
        else
        {
            LOG_ERROR(LOGGER_DEFAULT(), "listen socket create err! errno={} errstr={}", errno, strerror(errno));
            if (errno == EMFILE)
            {
                LOG_ERROR(LOGGER_DEFAULT(), "socket reached limit!");
            }
        }
    }

}
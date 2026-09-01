#include "net/TcpServer.h"
#include "net/base/Log.h"

#include <sstream>

namespace net
{
    EventLoop *CheckLoopNotNull(EventLoop *loop)
    {
        if (loop == nullptr)
        {
            LOG_FATAL("mainloop is null!");
        }
        return loop;
    }

    TcpServer::TcpServer(EventLoop *loop, Address::ptr listenAddr, const std::string &nameArg, Option option)
        : m_loop(CheckLoopNotNull(loop)), m_ipPort(listenAddr->to_string()), m_name(nameArg), m_acceptor(new Acceptor(m_loop, listenAddr, option == kReusePort)), m_threadPool(new EventLoopThreadPool(m_loop, m_name)), m_connectionCallback(), m_messageCallback(), m_started(0), m_nextConnId(1)
    {
        m_acceptor->setNewConnectionCallback(std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
    }

    TcpServer::~TcpServer()
    {
        m_loop->assertInLoopThread();
        for (auto &item : m_connections)
        {
            TcpConnection::ptr conn(item.second); // 这个局部的shared_ptr智能指针对象，出右括号，可以自动释放new
            item.second.reset();

            // 销毁连接
            conn->getLoop()->runInLoop(
                std::bind(&TcpConnection::connectDestroyed, conn));
        }
    }

    void TcpServer::setThreadNum(int numSubThreads)
    {
        m_threadPool->setThreadNum(numSubThreads);
    }

    void TcpServer::start()
    {
        if (m_started++ == 0)
        { // 防止一个TcpServer对象被start多次
            m_threadPool->start(m_threadInitCallback);
            m_loop->runInLoop(std::bind(&Acceptor::listen, m_acceptor.get()));
        }
    }

    void TcpServer::newConnection(int sockfd, Address::ptr peerAddr)
    {
        m_loop->assertInLoopThread();

        // 轮询算法，选择一个subLoop，来管理channel
        EventLoop *ioLoop = m_threadPool->getNextLoop();
        std::stringstream ss;
        ss << m_name << "-" << m_ipPort << "-" << m_nextConnId;
        ++m_nextConnId;
        std::string connName = ss.str();
        LOG_DEBUG("new connection [{}]  from {}", connName, peerAddr->to_string());

        // 通过sockfd获取其绑定的本机的IP地址和端口信息
        sockaddr_storage local;
        ::bzero(&local, sizeof local);
        socklen_t addrLen = sizeof local;
        if (::getsockname(sockfd, (sockaddr *)&local, &addrLen) < 0)
        {
            LOG_ERROR("sockets::getLocalAddr");
        }

        Address::ptr localAddr = Address::create(local, addrLen);

        // 根据连接成功的sockfd，创建TcpConnection连接对象
        TcpConnection::ptr conn(new TcpConnection(ioLoop, connName, sockfd, localAddr, peerAddr));
        m_connections[connName] = conn;
        // 下面回调都是用户设置给Tcpserver =》TcpConnection = 》 channel =》 poller =》notify channel调用回调
        conn->setConnectionCallback(m_connectionCallback);
        conn->setMessageCallback(m_messageCallback);
        conn->setWriteCompleteCallback(m_writeCompleteCallback);

        // 设置了如何关闭连接的回调
        conn->setCloseCallback(
            std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));
        ioLoop->runInLoop(std::bind(&TcpConnection::connectEstablished, conn));
    }

    void TcpServer::removeConnection(const TcpConnection::ptr &conn)
    {
        m_loop->runInLoop(
            std::bind(&TcpServer::removeConnectionInLoop, this, conn));
    }

    void TcpServer::removeConnectionInLoop(const TcpConnection::ptr &conn)
    {
        m_loop->assertInLoopThread();

        LOG_DEBUG("connection [{}] close.", conn->name());

        m_connections.erase(conn->name());

        EventLoop *ioLoop = conn->getLoop();
        ioLoop->queueInLoop(
            std::bind(&TcpConnection::connectDestroyed, conn));
    }
}
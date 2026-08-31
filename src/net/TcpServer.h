#pragma once

#include "net/EventLoop.h"
#include "net/EventLoopThreadPool.h"
#include "net/CallBacks.h"
#include "net/Acceptor.h"
#include "net/TcpConnection.h"

namespace net
{
    class TcpServer
    {
    public:
        using ThreadInitCallback = std::function<void(EventLoop *)>;
        enum Option
        {
            kNoReusePort,
            kReusePort,
        };
        TcpServer(EventLoop *loop, Address::ptr listenAddr, const std::string &nameArg, Option option = kNoReusePort);
        ~TcpServer();
        TcpServer(const TcpServer &) = delete;
        TcpServer &operator=(const TcpServer &) = delete;

        const std::string &ipPort() const { return m_ipPort; }
        const std::string &name() const { return m_name; }

        void setThreadInitCallback(const ThreadInitCallback &cb) { m_threadInitCallback = cb; }
        void setConnectionCallback(const ConnectionCallback &cb) { m_connectionCallback = cb; }
        void setMessageCallback(const MessageCallback &cb) { m_messageCallback = cb; }
        void setWriteCompleteCallback(const WriteCompleteCallback &cb) { m_writeCompleteCallback = cb; }

        // 设置subloop线程数量
        void setThreadNum(int numThreads);

        // 开启服务监听
        void start();

    private:
        void newConnection(int sockfd, Address::ptr peerAddr);
        void removeConnection(const TcpConnection::ptr &conn);
        void removeConnectionInLoop(const TcpConnection::ptr &conn);

        using ConnectionMap = std::unordered_map<std::string, TcpConnection::ptr>;

        EventLoop *m_loop; // 用户定义的loop

        const std::string m_ipPort;
        const std::string m_name;

        std::unique_ptr<Acceptor> m_acceptor; // 运行在mainloop，任务就是监听新连接事件

        std::shared_ptr<EventLoopThreadPool> m_threadPool; // one loop per thread

        ConnectionCallback m_connectionCallback;       // 新连接回调
        MessageCallback m_messageCallback;             // 有读写消息时的回调
        WriteCompleteCallback m_writeCompleteCallback; // 消息发送完成以后的回调

        ThreadInitCallback m_threadInitCallback; // loop线程初始化的回调
        std::atomic_int m_started;

        int m_nextConnId;
        ConnectionMap m_connections; // 保存所有的连接
    };
}
#pragma once

#include "net/TcpConnection.h"
#include "net/Connector.h"

#include <mutex>

namespace net
{
    class TcpClient
    {
        TcpClient(EventLoop *loop, const Address::ptr serverAddr, const std::string &nameArg = "");
        ~TcpClient();
        TcpClient(const TcpClient &) = delete;
        TcpClient &operator=(const TcpClient &) = delete;

        void connect();
        void disconnect();
        void stop();

        TcpConnection::ptr connection() const
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_connection;
        }

        EventLoop *getLoop() const { return m_loop; }
        void enableRetry() { m_retry = true; }

        const std::string &name() const
        {
            return m_name;
        }

        void setConnectionCallback(const ConnectionCallback &cb)
        {
            m_connectionCallback = cb;
        }

        void setMessageCallback(const MessageCallback &cb)
        {
            m_messageCallback = cb;
        }

        void setWriteCompleteCallback(const WriteCompleteCallback &cb)
        {
            m_writeCompleteCallback = cb;
        }

    private:
        void newConnection(int sockfd);
        void removeConnection(const TcpConnection::ptr &conn);

    private:
        EventLoop *m_loop;
        Connector::ptr m_connector;
        const std::string m_name;
        ConnectionCallback m_connectionCallback;
        MessageCallback m_messageCallback;
        WriteCompleteCallback m_writeCompleteCallback;
        bool m_retry;
        bool m_connect;

        int m_nextConnId;
        mutable std::mutex m_mutex;
        TcpConnection::ptr m_connection;
    };

}
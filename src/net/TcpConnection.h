#pragma once

#include "net/Buffer.h"
#include "net/base/Address.h"
#include "net/Channel.h"
#include "net/EventLoop.h"
#include "net/CallBacks.h"

namespace net
{

    class TcpConnection : public std::enable_shared_from_this<TcpConnection>
    {
    public:
        using ptr = std::shared_ptr<TcpConnection>;

        TcpConnection(EventLoop *loop, const std::string &name, int sockfd, Address::ptr localAddr, Address::ptr peerAddr);
        ~TcpConnection();

        TcpConnection(const TcpConnection &) = delete;
        TcpConnection &operator=(const TcpConnection &) = delete;

        EventLoop *getLoop() const { return m_loop; }
        const std::string &name() const { return m_name; }
        Address::ptr localAddress() const { return m_localAddr; }
        Address::ptr peerAddress() const { return m_peerAddr; }

        bool connected() const { return m_state == StateE::kConnected; }

        // 发送数据
        void send(const void *message, int len);
        void send(const std::string &buf);
        void send(Buffer *buf);
        // 关闭连接
        void shutdown();

        void setConnectionCallback(const ConnectionCallback &cb) { m_connectionCallback = cb; }
        void setMessageCallback(const MessageCallback &cb) { m_messageCallback = cb; }
        void setWriteCompleteCallback(const WriteCompleteCallback &cb) { m_writeCompleteCallback = cb; }
        void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t len)
        {
            m_highWaterMark = len;
            m_highWaterMarkCallback = cb;
        }
        void setCloseCallback(const CloseCallback &cb) { m_closeCallback = cb; }
        // 建立连接
        void connectEstablished();
        // 销毁连接
        void connectDestroyed();

        void forceClose();
        void forceCloseInLoop();

    private:
        void handleRead(Timestamp receiveTime);
        void handleWrite();
        void handleClose();
        void handleError();
        const char *stateToString() const;

        void sendInLoop(const void *message, size_t len);
        void shutdownInLoop();

    private:
        enum class StateE
        {
            kDisconnected,
            kConnecting,
            kConnected,
            kDisconnecting
        };

        void setState(StateE state) { m_state = state; }

        EventLoop *m_loop;
        const std::string m_name;
        StateE m_state;
        std::unique_ptr<Socket> m_socket;
        Channel::ptr m_channel;
        Address::ptr m_localAddr;
        Address::ptr m_peerAddr;
        ConnectionCallback m_connectionCallback;
        MessageCallback m_messageCallback;
        WriteCompleteCallback m_writeCompleteCallback;
        HighWaterMarkCallback m_highWaterMarkCallback;
        CloseCallback m_closeCallback;
        size_t m_highWaterMark;
        Buffer m_inputBuffer;
        Buffer m_outputBuffer;
    };

}
#pragma once

#include "net/Channel.h"

#include <functional>

namespace net
{
    class EventLoop;
    class Connector : public std::enable_shared_from_this<Connector>
    {
    public:
        typedef std::function<void(int sockfd)> NewConnectionCallback;

        Connector(EventLoop *loop, std::shared_ptr<Address> serverAddr);
        ~Connector();

        void setNewConnectionCallback(const NewConnectionCallback &cb)
        {
            m_newConnectionCallback = cb;
        }

        void start();
        void restart();
        void stop();

        std::shared_ptr<Address> serverAddress() const { return m_serverAddr; }

    private:
        enum class State
        {
            kDisconnected,
            kConnecting,
            kConnected
        };
        static const int kMaxRetryDelayMs;
        static const int kInitRetryDelayMs;

        const char *stateToString() const;
        void setState(State s) { m_state = s; }
        void startInLoop();
        void stopInLoop();
        virtual void connect();
        void connecting(int sockfd);
        void handleWrite();
        void handleError();
        void retry(int sockfd);
        int removeAndResetChannel();
        void resetChannel();

    private:
        EventLoop *m_loop;
        std::shared_ptr<Address> m_serverAddr;
        bool m_connect;
        State m_state;
        Channel::ptr m_channel;
        NewConnectionCallback m_newConnectionCallback;
        int m_retryDelayMs;
    };

}
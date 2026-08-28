#include "net/Connector.h"
#include "EventLoop.h"
#include "logger/log.h"
#include "net/base/SocketOps.h"

#include <errno.h>
#include <sstream>
#include <iostream>

namespace net
{
    const int Connector::kMaxRetryDelayMs = 30 * 1000;
    const int Connector::kInitRetryDelayMs = 500;

    Connector::Connector(EventLoop *loop, std::shared_ptr<Address> serverAddr)
        : m_loop(loop),
          m_serverAddr(serverAddr),
          m_connect(false),
          m_state(State::kDisconnected),
          m_retryDelayMs(kInitRetryDelayMs)
    {
    }

    Connector::~Connector()
    {
    }

    void Connector::start()
    {
        m_connect = true;
        m_loop->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
    }

    const char *Connector::stateToString() const
    {
        switch (m_state)
        {
        case State::kDisconnected:
            return "kDisconnected";
        case State::kConnecting:
            return "kConnecting";
        case State::kConnected:
            return "kConnected";
        default:
            return "unknown state";
        }
    }

    void Connector::startInLoop()
    {
        m_loop->assertInLoopThread();
        // assert(state_ == kDisconnected);
        if (m_state != State::kDisconnected)
            return;

        if (m_connect)
        {
            connect();
        }
        else
        {
            LOG_ERROR(LOGGER_DEFAULT(), "do not connect:{}", m_serverAddr->to_string());
        }
    }

    void Connector::stop()
    {
        m_connect = false;
        m_loop->queueInLoop(std::bind(&Connector::stopInLoop, shared_from_this())); // FIXME: unsafe
        // FIXME: cancel timer
    }

    void Connector::stopInLoop()
    {
        m_loop->assertInLoopThread();

        if (m_state == State::kConnecting)
        {
            setState(State::kDisconnected);
            int sockfd = removeAndResetChannel();
            retry(sockfd);
        }
    }

    void Connector::connect()
    {
        int sockfd = createNonblockingOrDie(m_serverAddr->get_family());
        int ret = ::connect(sockfd, m_serverAddr->get_addr(), m_serverAddr->get_addrLen());

        int savedErrno = (ret == 0) ? 0 : errno;
        switch (savedErrno)
        {
        case 0:
        case EINPROGRESS:
        case EINTR:
        case EISCONN:
            connecting(sockfd);
            break;

        case EAGAIN:
        case EADDRINUSE:
        case EADDRNOTAVAIL:
        case ECONNREFUSED:
        case ENETUNREACH:
            retry(sockfd);
            break;

        case EACCES:
        case EPERM:
        case EAFNOSUPPORT:
        case EALREADY:
        case EBADF:
        case EFAULT:
        case ENOTSOCK:
            LOG_ERROR(LOGGER_DEFAULT(), "connect error. errno={} errstr={}", savedErrno, strerror(savedErrno));
            ::close(sockfd);
            break;

        default:
            LOG_ERROR(LOGGER_DEFAULT(), "Unexpected error. errno={} errstr={}", savedErrno, strerror(savedErrno));
            ::close(sockfd);
            // connectErrorCallback_();
            break;
        }
    }

    void Connector::restart()
    {
        m_loop->assertInLoopThread();
        setState(State::kDisconnected);
        m_retryDelayMs = kInitRetryDelayMs;
        m_connect = true;
        startInLoop();
    }

    void Connector::connecting(int sockfd)
    {
        setState(State::kConnecting);
        // assert(!channel_);
        m_channel.reset(new Channel(m_loop, sockfd));
        m_channel->setWriteCallback(std::bind(&Connector::handleWrite, this)); // FIXME: unsafe
        m_channel->setErrorCallback(std::bind(&Connector::handleError, this)); // FIXME: unsafe

        // channel_->tie(shared_from_this()); is not working,
        // as channel_ is not managed by shared_ptr
        m_channel->enableWriting();
    }

    int Connector::removeAndResetChannel()
    {
        m_channel->disableAll();
        m_channel->remove();
        int sockfd = m_channel->fd();
        // Can't reset channel_ here, because we are inside Channel::handleEvent
        m_loop->queueInLoop(std::bind(&Connector::resetChannel, shared_from_this())); // FIXME: unsafe
        return sockfd;
    }

    void Connector::resetChannel()
    {
        m_channel.reset();
    }

    void Connector::handleWrite()
    {
        if (m_state == State::kConnecting)
        {
            int sockfd = removeAndResetChannel();
            int err = getSocketError(sockfd);
            if (err)
            {
                LOG_WARN(LOGGER_DEFAULT(), "Connector::handleWrite - SO_ERROR. errno={} errstr={}", err, strerror(err));
                retry(sockfd);
            }
            else if (isSelfConnect(sockfd))
            {
                LOG_WARN(LOGGER_DEFAULT(), "Self connect");
                retry(sockfd);
            }
            else
            {
                setState(State::kConnected);
                if (m_connect)
                {
                    // newConnectionCallback_指向TcpClient::newConnection(int sockfd)
                    m_newConnectionCallback(sockfd);
                }
                else
                {
                    ::close(sockfd);
                }
            }
        }
        else
        {
            // what happened?
            // assert(state_ == kDisconnected);
            if (m_state != State::kDisconnected)
                LOG_DEBUG(LOGGER_DEFAULT(), "state_ != kDisconnected");
        }
    }

    void Connector::handleError()
    {
        LOG_DEBUG(LOGGER_DEFAULT(), "Connector::handleError state={}", stateToString());
        if (m_state == State::kConnecting)
        {
            int sockfd = removeAndResetChannel();
            int err = getSocketError(sockfd);
            LOG_DEBUG(LOGGER_DEFAULT(), "SO_ERROR errno={} errstr={}", err, strerror(err));
            LOG_ERROR(LOGGER_DEFAULT(), "Connector::handleError state={}", stateToString());
            retry(sockfd);
        }
    }

    void Connector::retry(int sockfd)
    {
        close(sockfd);
        setState(State::kDisconnected);
        if (m_connect)
        {
            // LOG_INFO(LOGGER_DEFAULT(), "Connector::retry - Retry connecting to {} in {} milliseconds", m_serverAddr->to_string(), m_retryDelayMs);
            //  loop_->runAfter(retryDelayMs_/1000.0,
            //                  std::bind(&Connector::startInLoop, shared_from_this()));
            //  retryDelayMs_ = std::min(retryDelayMs_ * 2, kMaxRetryDelayMs);
            //  定时器重试， todo
            //  m_loop->addTimer(m_retryDelayMs, std::bind(&Connector::startInLoop, shared_from_this()));
            //  m_retryDelayMs = std::min(m_retryDelayMs * 2, kMaxRetryDelayMs);

            m_loop->runInLoop(std::bind(&Connector::startInLoop, this)); // FIXME: unsafe
        }
        else
        {
            LOG_DEBUG(LOGGER_DEFAULT(), "do not connect");
        }
    }

}
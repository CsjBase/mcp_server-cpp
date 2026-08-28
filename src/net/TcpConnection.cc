#include "net/TcpConnection.h"
#include "logger/log.h"

namespace net
{

    void defaultConnectionCallback(const TcpConnection::ptr &conn)
    {
        LOG_DEBUG(LOGGER_DEFAULT(), "{} -> {} is {}", conn->localAddress()->to_string(), conn->peerAddress()->to_string(), (conn->connected() ? "UP" : "DOWN"));
        // do not call conn->forceClose(), because some users want to register message callback only.
    }

    void defaultMessageCallback(const TcpConnection::ptr &, Buffer *buf, Timestamp)
    {
        buf->retrieveAll();
    }

    TcpConnection::TcpConnection(EventLoop *loop, const std::string &name, int sockfd, Address::ptr localAddr, Address::ptr peerAddr)
        : m_loop(loop), m_name(name), m_state(StateE::kConnecting), m_socket(new Socket(sockfd)),
          m_channel(new Channel(m_loop, m_socket->fd())), m_localAddr(localAddr), m_peerAddr(peerAddr), m_highWaterMark(64 * 1024 * 1024)
    {
        m_channel->setReadCallback(std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
        m_channel->setWriteCallback(std::bind(&TcpConnection::handleWrite, this));
        m_channel->setCloseCallback(std::bind(&TcpConnection::handleClose, this));
        m_channel->setErrorCallback(std::bind(&TcpConnection::handleError, this));

        LOG_DEBUG(LOGGER_DEFAULT(), "TcpConnection::ctor[{}] at 0x{} fd={}", m_name, (void *)this, m_socket->fd());
        m_socket->setKeepAlive(true);
    }

    TcpConnection::~TcpConnection()
    {
        LOG_DEBUG(LOGGER_DEFAULT(), "TcpConnection::dtor[{}] at fd={} state={}", m_name.c_str(), m_channel->fd(), stateToString());
    }

    void TcpConnection::send(const void *message, int len)
    {
        if (m_state == StateE::kConnected)
        {
            if (m_loop->isInLoopThread())
            {
                sendInLoop(message, len);
            }
            else
            {
                // 不在当前线程发送，需创建数据副本，防止野指针
                m_loop->runInLoop([this, str = std::string(static_cast<const char *>(message), len)]()
                                  { sendInLoop(str.data(), str.size()); });
            }
        }
    }

    void TcpConnection::send(const std::string &buf)
    {
        if (m_state == StateE::kConnected)
        {
            if (m_loop->isInLoopThread())
            {
                sendInLoop(buf.data(), buf.size());
            }
            else
            {
                // 不在当前线程发送，buf必须是值捕获
                m_loop->runInLoop([this, buf]()
                                  { sendInLoop(buf.data(), buf.size()); });
            }
        }
    }

    void TcpConnection::send(Buffer *buf)
    {
        if (m_state == StateE::kConnected)
        {
            if (m_loop->isInLoopThread())
            {
                sendInLoop(buf->peek(), buf->readableBytes());
                buf->retrieveAll();
            }
            else
            {
                // 不在当前线程发送，必须先读出buf中的数据存到副本中
                m_loop->runInLoop([this, str = buf->retrieveAllAsString()]()
                                  { sendInLoop(str.data(), str.size()); });
            }
        }
    }

    void TcpConnection::shutdown()
    {
        if (m_state == StateE::kConnected)
        {
            setState(StateE::kDisconnecting);
            m_loop->runInLoop(
                std::bind(&TcpConnection::shutdownInLoop, this));
        }
    }

    void TcpConnection::connectEstablished()
    {
        m_loop->assertInLoopThread();

        setState(StateE::kConnected);
        m_channel->tie(shared_from_this()); // 连接创立完成，设置该连接的tie，即channel和TcpConnection绑定，TcpConnection销毁后不再调用回调函数
        m_channel->enableReading();         // 向poller注册channel的epollin事件

        // 新连接建立，执行回调
        m_connectionCallback(shared_from_this());
    }

    void TcpConnection::connectDestroyed()
    {
        m_loop->assertInLoopThread();

        if (m_state == StateE::kConnected)
        {
            setState(StateE::kDisconnected);
            m_channel->disableAll(); // 把channel的所有感兴趣的事件，从poller中delete
            m_connectionCallback(shared_from_this());
        }
        m_channel->remove(); // 把channel从poller中删除掉
    }

    void TcpConnection::handleRead(Timestamp receiveTime)
    {
        m_loop->assertInLoopThread();

        int savedErrno = 0;
        ssize_t n = m_inputBuffer.readFd(m_channel->fd(), &savedErrno);
        if (n > 0)
        {
            // 已连接的用户有可读事件发生了，调用该用户的传入的回调操作
            m_messageCallback(shared_from_this(), &m_inputBuffer, receiveTime);
        }
        else if (n == 0)
        {
            handleClose();
        }
        else
        {
            errno = savedErrno;
            LOG_ERROR(LOGGER_DEFAULT(), "TcpConnrction::handleRead! errno={} errstr={}", savedErrno, strerror(savedErrno));
            handleError();
        }
    }

    void TcpConnection::handleWrite()
    {
        m_loop->assertInLoopThread();

        if (m_channel->isWriting())
        {
            int saveErrno;
            ssize_t n = m_outputBuffer.writeFd(m_channel->fd(), &saveErrno);
            if (n > 0)
            {
                m_outputBuffer.retrieve(n);
                if (m_outputBuffer.readableBytes() == 0)
                {
                    m_channel->disableWriting();
                    if (m_writeCompleteCallback)
                    {
                        // 唤醒loop_对象的thread线程，执行回调
                        m_loop->queueInLoop(std::bind(m_writeCompleteCallback, shared_from_this()));
                    }
                    if (m_state == StateE::kDisconnecting)
                    {
                        shutdownInLoop();
                    }
                }
            }
            else
            {
                LOG_ERROR(LOGGER_DEFAULT(), "TcpConnection::handleWrite error. errno={} errstr={}", saveErrno, strerror(saveErrno));
            }
        }
        else
        {
            LOG_ERROR(LOGGER_DEFAULT(), "TcpConnection fd={} is down, no more writing", m_channel->fd());
        }
    }

    void TcpConnection::handleClose()
    {
        m_loop->assertInLoopThread();

        LOG_DEBUG(LOGGER_DEFAULT(), "TcpConnection::handleClose fd={} state={}", m_channel->fd(), stateToString());
        setState(StateE::kDisconnected);
        m_channel->disableAll();

        TcpConnection::ptr connPtr(shared_from_this());
        m_connectionCallback(connPtr); // 执行连接关闭的回调
        m_closeCallback(connPtr);      // 关闭连接的回调
    }

    void TcpConnection::handleError()
    {
        int optval;
        socklen_t optlen = sizeof optval;
        if (::getsockopt(m_channel->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "TcpConnection::handleError name:{}  errno={} errstr={}", m_name, errno, strerror(errno));
        }
        else
        {
            LOG_ERROR(LOGGER_DEFAULT(), "TcpConnection::handleError name:{}  - SO_ERROR:", m_name, optval);
        }
    }

    const char *TcpConnection::stateToString() const
    {
        switch (m_state)
        {
        case StateE::kDisconnected:
            return "kDisconnected";
        case StateE::kConnecting:
            return "kConnecting";
        case StateE::kConnected:
            return "kConnected";
        case StateE::kDisconnecting:
            return "kDisconnecting";
        default:
            return "unknown state";
        }
    }

    void TcpConnection::sendInLoop(const void *message, size_t len)
    {
        m_loop->assertInLoopThread();

        ssize_t nwrote = 0;
        size_t remainging = len;
        bool faultErrno = false;

        // 之前调过该connection的shutdown，不能再进行发送了
        if (m_state == StateE::kDisconnected)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "disconnected, give up writing");
            return;
        }

        // 表示channel_第一次开始写数据，而且缓冲区没有待发送数据
        if (!m_channel->isWriting() && m_outputBuffer.readableBytes() == 0)
        {
            nwrote = ::write(m_channel->fd(), message, len);
            if (nwrote >= 0)
            {
                remainging = len - nwrote;
                if (remainging == 0 && m_writeCompleteCallback)
                {
                    // 在这数据全部发送完成，就不用给channel设置epollout事件了
                    m_loop->queueInLoop(
                        std::bind(m_writeCompleteCallback, shared_from_this()));
                }
            }
            else
            {
                nwrote = 0;
                if (errno != EWOULDBLOCK)
                {
                    LOG_ERROR(LOGGER_DEFAULT(), "TcpConnection::sendInLoop! errno={} errstr={}", errno, strerror(errno));
                    if (errno == EPIPE || errno == ECONNRESET)
                    { // EPIPE  ECONNRESET
                        faultErrno = true;
                    }
                }
            }
        }

        // 说明当前这次write，并没有把数据全部发送出去，剩余的数据需要保存到发送缓冲区当中，然后给channel注册epollout事件，
        // poller发现tcp的发送缓冲区有空间，会通知相应的sock-channel，调用handleWrite回调方法
        // 也就是调用TcpConnection::handleWrite方法，把发送缓冲区中的数据全部发送完成
        if (!faultErrno && remainging > 0)
        {
            // 目前发送缓冲区剩余的待发送数据的长度
            size_t oldLen = m_outputBuffer.readableBytes();
            if (oldLen + remainging >= m_highWaterMark && oldLen < m_highWaterMark && m_highWaterMarkCallback)
            {
                m_loop->queueInLoop(std::bind(
                    m_highWaterMarkCallback, shared_from_this(), oldLen + remainging));
            }
            m_outputBuffer.append((char *)message + nwrote, remainging);
            if (!m_channel->isWriting())
            {
                m_channel->enableWriting(); // 这里一定要注册channel的写事件，否则poller不会给channel通知epollout
            }
        }
    }

    void TcpConnection::shutdownInLoop()
    {
        m_loop->assertInLoopThread();

        if (!m_channel->isWriting())
        {                              // 说明outputBuffer中的数据已经全部发送完成
            m_socket->shutdownWrite(); // 关闭写端
        }
    }

    void TcpConnection::forceClose()
    {
        if (m_state == StateE::kConnected || m_state == StateE::kDisconnecting)
        {
            setState(StateE::kDisconnecting);
            m_loop->queueInLoop(std::bind(&TcpConnection::forceCloseInLoop, shared_from_this()));
        }
    }

    void TcpConnection::forceCloseInLoop()
    {
        m_loop->assertInLoopThread();
        if (m_state == StateE::kConnected || m_state == StateE::kDisconnecting)
        {
            handleClose();
        }
    }

}
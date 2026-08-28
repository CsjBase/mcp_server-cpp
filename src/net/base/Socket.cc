#include "net/base/Socket.h"
#include "logger/log.h"

namespace net
{
    Socket::~Socket()
    {
        close();
    }

    bool Socket::bind_address(Address::ptr localaddr)
    {
        if (::bind(m_sock, localaddr->get_addr(), localaddr->get_addrLen()))
        {
            return false;
        }
        return true;
    }

    bool Socket::listen(int backlog)
    {
        if (::listen(m_sock, backlog))
        {
            LOG_ERROR(LOGGER_DEFAULT(), "listen error errno={} errstr={}", errno, strerror(errno));
            return false;
        }
        return true;
    }

    int Socket::accept(Address::ptr &peeraddr)
    {
        sockaddr_storage addr;
        socklen_t addrlen = sizeof addr;
        int newsock = ::accept4(m_sock, (sockaddr *)&addr, &addrlen, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (newsock < 0)
        {
            int savedErrno = errno;
            LOG_ERROR(LOGGER_DEFAULT(), "Socket::accept");
            switch (savedErrno)
            {
            case EAGAIN:
            case ECONNABORTED:
            case EINTR:
            case EPROTO: // ???
            case EPERM:
            case EMFILE: // per-process lmit of open file desctiptor ???
                // expected errors
                errno = savedErrno;
                break;
            case EBADF:
            case EFAULT:
            case EINVAL:
            case ENFILE:
            case ENOBUFS:
            case ENOMEM:
            case ENOTSOCK:
            case EOPNOTSUPP:
                // unexpected errors
                LOG_FATAL(LOGGER_DEFAULT(), "unexpected error of ::accept errno={} errstr={}", errno, strerror(errno));
                break;
            default:
                LOG_FATAL(LOGGER_DEFAULT(), "unknown error of ::accept errno={} errstr={}", errno, strerror(errno));
                break;
            }
        }
        peeraddr = Address::create(addr, addrlen);
        return newsock;
    }

    void Socket::shutdownWrite()
    {
        if (::shutdown(m_sock, SHUT_WR) < 0)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "sockets::shutdown_write error!");
        }
    }

    bool Socket::setTcpNoDelay(bool on)
    {
        int optval = on ? 1 : 0;
        return set_option(IPPROTO_TCP, TCP_NODELAY, &optval, static_cast<socklen_t>(sizeof optval));
    }

    bool Socket::setReuseAddr(bool on)
    {
        int optval = on ? 1 : 0;
        return set_option(SOL_SOCKET, SO_REUSEADDR, &optval, static_cast<socklen_t>(sizeof optval));
    }

    bool Socket::setReusePort(bool on)
    {
        bool ret = true;
#ifdef SO_REUSEPORT
        int optval = on ? 1 : 0;
        ret = set_option(SOL_SOCKET, SO_REUSEPORT, &optval, static_cast<socklen_t>(sizeof optval));
        if (!ret && on)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "SO_REUSEPORT failed.");
        }
#else
        if (on)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "SO_REUSEPORT is not supported.");
            return false;
        }
#endif
        return ret;
    }

    bool Socket::setKeepAlive(bool on)
    {
        int optval = on ? 1 : 0;
        return set_option(SOL_SOCKET, SO_KEEPALIVE, &optval, static_cast<socklen_t>(sizeof optval));
    }

    bool Socket::set_option(int level, int option, const void *result, socklen_t len)
    {
        if (setsockopt(m_sock, level, option, result, (socklen_t)len))
        {
            LOG_ERROR(LOGGER_DEFAULT(), "set_option sock={} level={} option={} errno={} errstr=", m_sock, level, option, errno, strerror(errno));
            return false;
        }
        return true;
    }

    bool Socket::close()
    {
        if (m_sock == -1)
        {
            return true;
        }
        else
        {
            ::close(m_sock);
            m_sock = -1;
        }
        return true;
    }

    bool Socket::connect(Address::ptr addr)
    {
        if (::connect(m_sock, addr->get_addr(), addr->get_addrLen()) == -1)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "sock={} connect({}) error errno={} errstr={}", m_sock, addr->to_string(), errno, strerror(errno));
            return false;
        }

        return true;
    }

    int Socket::send(const void *buffer, size_t length, int flags)
    {
        return ::send(m_sock, buffer, length, flags);
    }

    int Socket::send(const iovec *buffers, size_t length, int flags)
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec *)buffers;
        msg.msg_iovlen = length;
        return ::sendmsg(m_sock, &msg, flags);
    }

    int Socket::sendto(const void *buffer, size_t length, Address::ptr to, int flags)
    {
        return ::sendto(m_sock, buffer, length, flags, to->get_addr(), to->get_addrLen());
    }

    int Socket::sendto(const iovec *buffers, size_t length, Address::ptr to, int flags)
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec *)buffers;
        msg.msg_iovlen = length;
        msg.msg_name = (void *)to->get_addr();
        msg.msg_namelen = to->get_addrLen();
        return ::sendmsg(m_sock, &msg, flags);
    }

    int Socket::recv(void *buffer, size_t length, int flags)
    {
        return ::recv(m_sock, buffer, length, flags);
    }

    int Socket::recv(iovec *buffers, size_t length, int flags)
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec *)buffers;
        msg.msg_iovlen = length;
        return ::recvmsg(m_sock, &msg, flags);
    }

    int Socket::recvfrom(void *buffer, size_t length, Address::ptr from, int flags)
    {
        socklen_t len = from->get_addrLen();
        return ::recvfrom(m_sock, buffer, length, flags, from->get_addr(), &len);
    }

    int Socket::recvfrom(iovec *buffers, size_t length, Address::ptr from, int flags)
    {
        msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = (iovec *)buffers;
        msg.msg_iovlen = length;
        msg.msg_name = from->get_addr();
        msg.msg_namelen = from->get_addrLen();
        return ::recvmsg(m_sock, &msg, flags);
    }
}
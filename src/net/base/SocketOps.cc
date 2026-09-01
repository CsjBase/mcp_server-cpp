#include "net/base/SocketOps.h"
#include "net/base/Log.h"

#include <stdexcept>
#include <sys/fcntl.h>
#include <sys/eventfd.h>

namespace net
{

    void fromIpPort(const char *ip, uint16_t port, sockaddr_in *addr)
    {
        addr->sin_family = AF_INET;
        addr->sin_port = htobe16(port);
        if (::inet_pton(AF_INET, ip, &addr->sin_addr) <= 0)
        {
            LOG_ERROR("fromIpPort failed");
        }
    }

    void fromIpPort(const char *ip, uint16_t port, sockaddr_in6 *addr)
    {

        addr->sin6_family = AF_INET6;
        addr->sin6_port = htobe16(port);
        if (::inet_pton(AF_INET6, ip, &addr->sin6_addr) <= 0)
        {
            LOG_ERROR("fromIpPort failed");
        }
    }

    int createNonblockingOrDie(sa_family_t family)
    {
        int sockfd = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
        if (sockfd < 0)
        {
            LOG_FATAL("sockets::createNonblockingOrDie error");
            return sockfd;
        }

        setNonBlockAndCloseOnExec(sockfd);
        return sockfd;
    }

    void setNonBlockAndCloseOnExec(int sockfd)
    {
        // non-block
        int flags = ::fcntl(sockfd, F_GETFL, 0);
        if (flags == -1)
        {
            LOG_ERROR("::fcntl({}, F_GETFL, 0) errno={} errstr={}", sockfd, errno, strerror(errno));
        }
        flags |= O_NONBLOCK;
        int ret = ::fcntl(sockfd, F_SETFL, flags);
        if (ret == -1)
        {
            LOG_FATAL("::fcntl({}) set NonBlock err! errno={} errstr={}", sockfd,
                      errno, strerror(errno));
        }

        // close-on-exec
        flags = ::fcntl(sockfd, F_GETFD, 0);
        if (flags == -1)
        {
            LOG_ERROR("::fcntl({}, F_GETFD, 0) errno={} errstr={}", sockfd, errno, strerror(errno));
        }
        flags |= FD_CLOEXEC;
        ret = ::fcntl(sockfd, F_SETFD, flags);
        if (ret == -1)
        {
            LOG_FATAL("::fcntl({}) set close-on-exec err! errno={} errstr={}", sockfd, errno, strerror(errno));
        }
    }

    int createEventfd()
    {
        int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (evtfd < 0)
        {
            LOG_FATAL("eventfd error! errno:{} errstr={}", errno, strerror(errno));
        }
        return evtfd;
    }

    int getSocketError(int sockfd)
    {
        int optval;
        socklen_t optlen = static_cast<socklen_t>(sizeof optval);

        if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
        {
            return errno;
        }
        else
        {
            return optval;
        }
    }
    Address::ptr getLocalAddr(int sockfd)
    {
        sockaddr_storage localaddr;
        socklen_t addrlen = static_cast<socklen_t>(sizeof localaddr);
        memset(&localaddr, 0, sizeof localaddr);
        if (::getsockname(sockfd, (sockaddr *)(&localaddr), &addrlen) < 0)
        {
            LOG_ERROR("get fd:{} addr err", sockfd);
        }
        switch (localaddr.ss_family)
        {
        case AF_INET:
        {
            sockaddr_in *addr = reinterpret_cast<sockaddr_in *>(&localaddr);
            return std::make_shared<InetAddress>(*addr);
        }

        case AF_INET6:
        {
            sockaddr_in6 *addr = reinterpret_cast<sockaddr_in6 *>(&localaddr);
            return std::make_shared<InetAddress>(*addr);
        }

        case AF_UNIX:
        {
            sockaddr_un *addr = reinterpret_cast<sockaddr_un *>(&localaddr);
            return std::make_shared<UnixAddress>(*addr);
        }

        default:
            return nullptr;
        }
        return nullptr;
    }

    Address::ptr getPeerAddr(int sockfd)
    {
        sockaddr_storage peeraddr;
        socklen_t addrlen = static_cast<socklen_t>(sizeof peeraddr);
        memset(&peeraddr, 0, sizeof peeraddr);
        if (::getpeername(sockfd, (sockaddr *)(&peeraddr), &addrlen) < 0)
        {
            LOG_ERROR("get fd:{} addr err", sockfd);
        }

        switch (peeraddr.ss_family)
        {
        case AF_INET:
        {
            sockaddr_in *addr = reinterpret_cast<sockaddr_in *>(&peeraddr);
            return std::make_shared<InetAddress>(*addr);
        }

        case AF_INET6:
        {
            sockaddr_in6 *addr = reinterpret_cast<sockaddr_in6 *>(&peeraddr);
            return std::make_shared<InetAddress>(*addr);
        }

        case AF_UNIX:
        {
            sockaddr_un *addr = reinterpret_cast<sockaddr_un *>(&peeraddr);
            return std::make_shared<UnixAddress>(*addr);
        }

        default:
            return nullptr;
        }
        return nullptr;
    }
    bool isSelfConnect(int sockfd)
    {
        Address::ptr localaddr = getLocalAddr(sockfd);
        Address::ptr peeraddr = getPeerAddr(sockfd);
        if (localaddr->get_family() == AF_INET)
        {
            const sockaddr_in *laddr4 = reinterpret_cast<sockaddr_in *>(localaddr->get_addr());
            const sockaddr_in *raddr4 = reinterpret_cast<sockaddr_in *>(peeraddr->get_addr());
            return laddr4->sin_port == raddr4->sin_port && laddr4->sin_addr.s_addr == raddr4->sin_addr.s_addr;
        }
        else if (localaddr->get_family() == AF_INET6)
        {
            const sockaddr_in6 *laddr6 = reinterpret_cast<sockaddr_in6 *>(localaddr->get_addr());
            const sockaddr_in6 *raddr6 = reinterpret_cast<sockaddr_in6 *>(peeraddr->get_addr());
            return laddr6->sin6_port == raddr6->sin6_port && memcmp(&(laddr6->sin6_addr), &(raddr6->sin6_addr), sizeof(sockaddr_in6)) == 0;
        }
        else if (localaddr->get_family() == AF_UNIX)
        {
            const sockaddr_un *laddr = reinterpret_cast<sockaddr_un *>(localaddr->get_addr());
            const sockaddr_un *raddr = reinterpret_cast<sockaddr_un *>(peeraddr->get_addr());
            return memcmp(laddr->sun_path, raddr->sun_path, sizeof(sockaddr_un)) == 0;
        }
        else
        {
            return false;
        }
    }
}
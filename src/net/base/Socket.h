#pragma once

#include "net/base/Address.h"

#include <netinet/tcp.h>

namespace net
{
    class Socket
    {
    public:
        explicit Socket(int sockfd)
            : m_sock(sockfd)
        {
        }

        ~Socket();
        Socket(const Socket &) = delete;
        Socket &operator=(const Socket &) = delete;

        int fd() const { return m_sock; }

        bool bind_address(Address::ptr localaddr);

        bool listen(int backlog = SOMAXCONN);

        int accept(Address::ptr &peeraddr);

        void shutdownWrite();

        bool setTcpNoDelay(bool on);
        bool setReuseAddr(bool on);
        bool setReusePort(bool on);
        bool setKeepAlive(bool on);

        bool set_option(int level, int option, const void *result, socklen_t len);

        bool close();

        bool connect(Address::ptr addr);

        int send(const void *buffer, size_t length, int flags = 0);
        int send(const iovec *buffers, size_t length, int flags = 0);
        int sendto(const void *buffer, size_t length, Address::ptr to, int flags = 0);
        int sendto(const iovec *buffers, size_t length, Address::ptr to, int flags = 0);
        int recv(void *buffer, size_t length, int flags = 0);
        int recv(iovec *buffers, size_t length, int flags = 0);
        int recvfrom(void *buffer, size_t length, Address::ptr from, int flags = 0);
        int recvfrom(iovec *buffers, size_t length, Address::ptr from, int flags = 0);

    private:
        /// socket句柄
        int m_sock;
    };
}
#pragma once

#include "net/base/Address.h"

#include <arpa/inet.h>

namespace net
{
    void fromIpPort(const char *ip, uint16_t port, sockaddr_in *addr);
    void fromIpPort(const char *ip, uint16_t port, sockaddr_in6 *addr);

    int createNonblockingOrDie(sa_family_t family);
    void setNonBlockAndCloseOnExec(int sockfd);
    int createEventfd();
    int getSocketError(int sockfd);
    Address::ptr getLocalAddr(int sockfd);
    Address::ptr getPeerAddr(int sockfd);
    bool isSelfConnect(int sockfd);
}

#pragma once

#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <memory>

namespace net
{

    /**
     * @brief Socket地址的基类,抽象类
     */
    class Address
    {
    public:
        using ptr = std::shared_ptr<Address>;
        /**
         * @brief 虚析构函数
         */
        virtual ~Address() {}

        /**
         * @brief 返回协议簇
         */
        int get_family() const;

        /**
         * @brief 返回sockaddr指针,只读
         */
        virtual const sockaddr *get_addr() const = 0;

        /**
         * @brief 返回sockaddr指针,读写
         */
        virtual sockaddr *get_addr() = 0;

        /**
         * @brief 返回sockaddr的长度
         */
        virtual socklen_t get_addrLen() const = 0;

        /**
         * @brief 返回可读性字符串
         */
        std::string to_string() const;

        /**
         * @brief 可读性输出地址
         */
        virtual std::ostream &insert(std::ostream &os) const = 0;

        static Address::ptr create(const sockaddr_storage &addr, socklen_t len);
    };

    /**
     * @brief 网络socket地址
     */
    class InetAddress : public Address
    {
    public:
        explicit InetAddress(uint16_t port, bool loopbackOnly = false, bool ipv6 = false);

        /**
         * @brief 使用给定的ip和端口构造
         * @param[in] ip IP地址字符串
         * @param[in] port 端口号
         * @param[in] ipv6 是否构造IPv6地址 默认false
         */
        InetAddress(std::string ip, uint16_t port, bool ipv6 = false);

        explicit InetAddress(const struct sockaddr_in &addr)
            : m_addr(addr)
        {
        }

        explicit InetAddress(const struct sockaddr_in6 &addr)
            : m_addr_6(addr)
        {
        }

        const sockaddr *get_addr() const override;
        sockaddr *get_addr() override;
        socklen_t get_addrLen() const override;

        // void setSockAddrInet6(const struct sockaddr_in6 &addr6) { addr_6 = addr6; }

        uint32_t get_port() const;

        /**
         * @brief 输出可读字符串到流
         */
        std::ostream &insert(std::ostream &os) const override;

    private:
        union
        {
            struct sockaddr_in m_addr;
            struct sockaddr_in6 m_addr_6;
        };
    };

    class UnixAddress : public Address
    {
    public:
        UnixAddress();
        explicit UnixAddress(const std::string &path);
        UnixAddress(const struct sockaddr_un &addr, socklen_t len = sizeof(sockaddr_un));

        const sockaddr *get_addr() const override;
        sockaddr *get_addr() override;
        socklen_t get_addrLen() const override;
        void set_addrLen(uint32_t v);

        std::string get_path() const;

        std::ostream &insert(std::ostream &os) const override;

    private:
        sockaddr_un m_addr;
        socklen_t m_length;
    };

    /**
     * @brief 流式输出Address
     */
    std::ostream &operator<<(std::ostream &os, const Address &addr);

}

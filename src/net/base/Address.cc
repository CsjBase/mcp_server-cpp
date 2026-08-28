#include "net/base/Address.h"
#include "net/base/endian.h"
#include "net/base/SocketOps.h"
#include "logger/log.h"

#include <sstream>

namespace net
{
    int Address::get_family() const
    {
        return get_addr()->sa_family;
    }

    std::string Address::to_string() const
    {
        std::stringstream ss;
        insert(ss);
        return ss.str();
    }

    Address::ptr Address::create(const sockaddr_storage &storage, socklen_t len)
    {
        switch (storage.ss_family)
        {
        case AF_INET:
        {
            const sockaddr_in *addr4 = reinterpret_cast<const sockaddr_in *>(&storage);
            return std::make_shared<InetAddress>(*addr4);
        }
        case AF_INET6:
        {
            const sockaddr_in6 *addr6 = reinterpret_cast<const sockaddr_in6 *>(&storage);
            return std::make_shared<InetAddress>(*addr6);
        }
        case AF_UNIX:
        {
            const sockaddr_un *addr_un = reinterpret_cast<const sockaddr_un *>(&storage);
            return std::make_shared<UnixAddress>(*addr_un, len);
        }
        default:
            return nullptr;
        }
    }

    std::ostream &InetAddress::insert(std::ostream &os) const
    {
        uint32_t addr = byteswapOnLittleEndian(m_addr.sin_addr.s_addr);
        os << ((addr >> 24) & 0xff) << "."
           << ((addr >> 16) & 0xff) << "."
           << ((addr >> 8) & 0xff) << "."
           << (addr & 0xff);
        os << ":" << byteswapOnLittleEndian(m_addr.sin_port);
        return os;
    }

    static_assert(offsetof(sockaddr_in, sin_family) == 0, "sin_family offset 0");
    static_assert(offsetof(sockaddr_in6, sin6_family) == 0, "sin6_family offset 0");
    static_assert(offsetof(sockaddr_in, sin_port) == 2, "sin_port offset 2");
    static_assert(offsetof(sockaddr_in6, sin6_port) == 2, "sin6_port offset 2");

    InetAddress::InetAddress(uint16_t port, bool loopbackOnly, bool ipv6)
    {
        if (ipv6)
        {
            memset(&m_addr_6, 0, sizeof m_addr_6);
            m_addr_6.sin6_family = AF_INET6;
            in6_addr ip = loopbackOnly ? in6addr_loopback : in6addr_any;
            m_addr_6.sin6_addr = ip;
            m_addr_6.sin6_port = htobe16(port);
        }
        else
        {
            memset(&m_addr, 0, sizeof m_addr);
            m_addr.sin_family = AF_INET;
            in_addr_t ip = loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY;
            m_addr.sin_addr.s_addr = ip;
            m_addr.sin_port = htobe16(port);
        }
    }

    InetAddress::InetAddress(std::string ip, uint16_t port, bool ipv6)
    {
        if (ipv6 || strchr(ip.c_str(), ':'))
        {
            memset(&m_addr_6, 0, sizeof m_addr_6);
            fromIpPort(ip.c_str(), port, &m_addr_6);
        }
        else
        {
            memset(&m_addr, 0, sizeof m_addr);
            fromIpPort(ip.c_str(), port, &m_addr);
        }
    }

    const sockaddr *InetAddress::get_addr() const
    {
        return reinterpret_cast<const sockaddr *>(&m_addr);
    }

    sockaddr *InetAddress::get_addr()
    {
        return reinterpret_cast<sockaddr *>(&m_addr);
    }

    socklen_t InetAddress::get_addrLen() const
    {
        return m_addr.sin_family == AF_INET ? sizeof m_addr : sizeof m_addr_6;
    }

    static constexpr size_t MAX_PATH_LEN = sizeof(((sockaddr_un *)0)->sun_path);

    UnixAddress::UnixAddress()
        : m_length(offsetof(struct sockaddr_un, sun_path))
    {
        // 清空整个结构体，确保所有字节为零
        memset(&m_addr, 0, sizeof(m_addr));

        // 设置地址族
        m_addr.sun_family = AF_UNIX;
    }

    UnixAddress::UnixAddress(const std::string &path)
    {
        // 清空结构体
        memset(&m_addr, 0, sizeof(m_addr));
        m_addr.sun_family = AF_UNIX;

        // 检查路径是否为空
        if (path.empty())
        {
            // 空路径：等同于默认构造，只设置sun_family
            m_length = offsetof(struct sockaddr_un, sun_path);
            return;
        }

        // 判断是否为抽象套接字（以'\0'开头）
        bool is_abstract = (path[0] == '\0');

        // 计算需要复制的路径长度
        // 普通路径：需要包含末尾的'\0'
        // 抽象套接字：不包含末尾的'\0'（因为路径以'\0'开头，本身已经是二进制数据）
        size_t path_len;
        if (is_abstract)
        {
            // 抽象套接字：路径长度就是字符串长度（不包括额外的结束符）
            path_len = path.size();
        }
        else
        {
            // 普通路径：需要包含结束符
            path_len = path.size() + 1;
        }

        // 检查路径长度是否超出限制
        if (path_len > MAX_PATH_LEN)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "Unix socket path too long: {} bytes (max: {})", std::to_string(path_len), std::to_string(MAX_PATH_LEN));
            return;
        }

        // 特殊检查：如果path包含多个'\0'，可能导致截断
        // 对于抽象套接字，允许路径中包含'\0'
        // 但对于普通路径，如果中间包含'\0'，会导致截断
        if (!is_abstract)
        {
            size_t first_null = path.find('\0');
            if (first_null != std::string::npos && first_null < path.size())
            {
                // 普通路径中间包含'\0'，这会导致路径被截断
                // 这是一个潜在的错误，但我们的实现会保留它（memcpy会复制所有内容）
                // 但系统调用时只会看到第一个'\0'
                // 可以选择抛出警告或直接忽略
                // 这里我们选择继续执行，但保留这种行为（与标准UNIX行为一致）
            }
        }

        // 复制路径数据到sun_path
        memcpy(m_addr.sun_path, path.c_str(), path_len);

        // 设置总长度：sun_family + sun_path的有效数据
        m_length = offsetof(struct sockaddr_un, sun_path) + path_len;

        // 安全检查：确保m_length不超过结构体大小
        if (m_length > sizeof(m_addr))
        {
            LOG_ERROR(LOGGER_DEFAULT(), "Internal error: calculated length exceeds sockaddr_un size");
        }
    }

    UnixAddress::UnixAddress(const struct sockaddr_un &addr, socklen_t len)
        : m_length(0)
    {
        // 验证输入参数
        if (addr.sun_family != AF_UNIX)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "Invalid address family: expected AF_UNIX ({}), got {}",
                      std::to_string(AF_UNIX), std::to_string(addr.sun_family));
        }

        // 验证长度参数
        if (len < offsetof(struct sockaddr_un, sun_path))
        {
            LOG_ERROR(LOGGER_DEFAULT(), "Invalid length: too short for sockaddr_un ({} < {})",
                      std::to_string(len), std::to_string(offsetof(struct sockaddr_un, sun_path)));
            return;
        }

        if (len > sizeof(sockaddr_un))
        {
            LOG_ERROR(LOGGER_DEFAULT(), "Invalid length: exceeds sockaddr_un size ({} > {})",
                      std::to_string(len), std::to_string(sizeof(sockaddr_un)));
            return;
        }

        // 复制整个结构体
        memcpy(&m_addr, &addr, sizeof(m_addr));
        m_length = len;

        // 验证sun_path的合法性
        size_t path_len = len - offsetof(struct sockaddr_un, sun_path);

        if (path_len > 0)
        {
            // 检查路径是否以'\0'开头（抽象套接字）
            bool is_abstract = (m_addr.sun_path[0] == '\0');

            if (!is_abstract)
            {
                // 对于普通路径，检查是否以'\0'结尾
                // 但要注意：如果path_len < sizeof(sun_path)，末尾可能没有'\0'
                // 因为参数可能来自bind/connect等系统调用返回的地址
                // 对于普通路径，有效的路径应该以'\0'结尾
                if (path_len > 0 && m_addr.sun_path[path_len - 1] != '\0')
                {
                    // 这不是致命错误，因为系统调用可能返回部分填充的结构体
                    // 但我们可以记录或发出警告（在生产代码中可能使用日志）
                    // 这里我们选择不做特殊处理，因为系统调用已经保证了有效性
                }
            }

            // 检查路径长度是否超限
            if (path_len > MAX_PATH_LEN)
            {
                LOG_ERROR(LOGGER_DEFAULT(), "Path length exceeds maximum: {} > {}",
                          std::to_string(path_len), std::to_string(MAX_PATH_LEN));
                return;
            }
        }
    }

    const sockaddr *UnixAddress::get_addr() const
    {
        return (sockaddr *)&m_addr;
    }
    sockaddr *UnixAddress::get_addr()
    {
        return (sockaddr *)&m_addr;
    }
    socklen_t UnixAddress::get_addrLen() const
    {
        return m_length;
    }

    void UnixAddress::set_addrLen(uint32_t v)
    {
        m_length = v;
    }

    std::string UnixAddress::get_path() const
    {
        if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0')
        {
            size_t path_len = m_length - offsetof(sockaddr_un, sun_path) - 1;
            return "\\0" + std::string(m_addr.sun_path + 1, path_len);
        }
        else
        {
            return std::string(m_addr.sun_path);
        }
    }
    std::ostream &UnixAddress::insert(std::ostream &os) const
    {
        if (m_length > offsetof(sockaddr_un, sun_path) && m_addr.sun_path[0] == '\0')
        {
            return os << "\\0" << std::string(m_addr.sun_path + 1, m_length - offsetof(sockaddr_un, sun_path) - 1);
        }
        return os << m_addr.sun_path;
    }

    uint32_t InetAddress::get_port() const
    {
        return byteswapOnLittleEndian(m_addr.sin_port);
    }

    std::ostream &operator<<(std::ostream &os, const Address &addr)
    {
        return addr.insert(os);
    }

}
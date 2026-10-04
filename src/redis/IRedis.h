#pragma once

#include <hiredis/hiredis.h>
#include <memory>
#include <sys/time.h>
#include <string>

namespace redis
{

    typedef std::shared_ptr<redisReply> ReplyPtr;

    /// 当前 Unix 毫秒时间戳(活跃时间统一语义)
    inline uint64_t nowMs()
    {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    /**
     * @brief Redis 连接统一基类
     *
     * 承载连接生命周期与元信息, 供连接池统一管理:
     * 池的借出/回收/淘汰只依赖 isConnected/close/lastActiveTime 等
     * 本接口操作, 命令接口由 ISyncRedis / IAsyncRedis 分别定义,
     * 连接池可写成 RedisPool<T> 模板同时支持同步与异步连接。
     */
    class IRedis
    {
    public:
        typedef std::shared_ptr<IRedis> ptr;
        IRedis() : m_logEnable(true), m_lastActiveTime(0) {}
        virtual ~IRedis() {}

        /// 连接是否可用
        virtual bool isConnected() const = 0;
        /// 断开当前连接(幂等; 池化回收/淘汰时调用)
        virtual void close() = 0;

        const std::string &getName() const { return m_name; }
        void setName(const std::string &v) { m_name = v; }

        const std::string &getPasswd() const { return m_passwd; }
        void setPasswd(const std::string &v) { m_passwd = v; }

        const std::string &getIp() const { return m_ip; }
        uint32_t getPort() const { return m_port; }
        void setAddr(const std::string &ip, int port)
        {
            m_ip = ip;
            m_port = (uint32_t)port;
        }

        uint64_t getLastActiveTime() const { return m_lastActiveTime; }
        void setLastActiveTime(uint64_t v) { m_lastActiveTime = v; }

        bool isLogEnable() const { return m_logEnable; }
        void setLogEnable(bool v) { m_logEnable = v; }

    protected:
        std::string m_name;
        std::string m_passwd;
        std::string m_ip = "127.0.0.1";
        uint32_t m_port = 6379;
        bool m_logEnable;
        uint64_t m_lastActiveTime;
    };

}

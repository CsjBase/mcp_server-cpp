#pragma once

#include <hiredis/hiredis.h>
#include <memory>
#include <vector>
#include <map>
#include <string>

namespace redis
{

    typedef std::shared_ptr<redisReply> ReplyPtr;
    class IRedis
    {
    public:
        typedef std::shared_ptr<IRedis> ptr;
        IRedis() : m_logEnable(true) {}
        virtual ~IRedis() {}

        virtual ReplyPtr cmd(const char *fmt, ...) = 0;
        virtual ReplyPtr cmd(const char *fmt, va_list ap) = 0;
        virtual ReplyPtr cmd(const std::vector<std::string> &argv) = 0;

        const std::string &getName() const { return m_name; }
        void setName(const std::string &v) { m_name = v; }

        const std::string &getPasswd() const { return m_passwd; }
        void setPasswd(const std::string &v) { m_passwd = v; }

    protected:
        std::string m_name;
        std::string m_passwd;
        bool m_logEnable;
    };

    class ISyncRedis : public IRedis
    {
    public:
        typedef std::shared_ptr<ISyncRedis> ptr;
        virtual ~ISyncRedis() {}

        virtual bool reconnect() = 0;
        virtual bool connect(const std::string &ip, int port, uint64_t ms = 0) = 0;
        virtual bool connect() = 0;
        virtual bool setTimeout(uint64_t ms) = 0;

        virtual int appendCmd(const char *fmt, ...) = 0;
        virtual int appendCmd(const char *fmt, va_list ap) = 0;
        virtual int appendCmd(const std::vector<std::string> &argv) = 0;

        virtual ReplyPtr getReply() = 0;

        uint64_t getLastActiveTime() const { return m_lastActiveTime; }
        void setLastActiveTime(uint64_t v) { m_lastActiveTime = v; }

    protected:
        uint64_t m_lastActiveTime;
    };

    class Redis : public ISyncRedis
    {
    public:
        typedef std::shared_ptr<Redis> ptr;
        Redis();
        Redis(const std::map<std::string, std::string> &conf);

        virtual bool reconnect();
        virtual bool connect(const std::string &ip, int port, uint64_t ms = 0);
        virtual bool connect();
        virtual bool setTimeout(uint64_t ms);

        virtual ReplyPtr cmd(const char *fmt, ...);
        virtual ReplyPtr cmd(const char *fmt, va_list ap);
        virtual ReplyPtr cmd(const std::vector<std::string> &argv);

        virtual int appendCmd(const char *fmt, ...);
        virtual int appendCmd(const char *fmt, va_list ap);
        virtual int appendCmd(const std::vector<std::string> &argv);

        virtual ReplyPtr getReply();

        /// 最近一次错误的描述(仅连接/命令失败后有意义)
        std::string getErrStr() const;

    private:
        std::string m_ip;
        uint32_t m_port;
        uint32_t m_connectMs;
        struct timeval m_cmdTimeout;
        std::shared_ptr<redisContext> m_context;
        std::string m_errStr; // 最近一次错误描述(context 释放后仍可查询)
    };

}
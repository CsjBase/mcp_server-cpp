#pragma once

#include "IRedis.h"

#include <cstdarg>
#include <cstdint>
#include <map>
#include <vector>

namespace redis
{
    /**
     * @brief 同步命令接口(阻塞式)
     */
    class ISyncRedis : public IRedis
    {
    public:
        typedef std::shared_ptr<ISyncRedis> ptr;
        virtual ~ISyncRedis() {}

        virtual bool reconnect() = 0;
        virtual bool connect(const std::string &ip, int port, uint64_t ms = 0) = 0;
        virtual bool connect() = 0;
        virtual bool setTimeout(uint64_t ms) = 0;

        virtual ReplyPtr cmd(const char *fmt, ...) = 0;
        virtual ReplyPtr cmd(const char *fmt, va_list ap) = 0;
        virtual ReplyPtr cmd(const std::vector<std::string> &argv) = 0;

        virtual int appendCmd(const char *fmt, ...) = 0;
        virtual int appendCmd(const char *fmt, va_list ap) = 0;
        virtual int appendCmd(const std::vector<std::string> &argv) = 0;

        virtual ReplyPtr getReply() = 0;
    };

    /**
     * @brief 同步客户端: hiredis redisContext 封装
     */
    class Redis : public ISyncRedis
    {
    public:
        typedef std::shared_ptr<Redis> ptr;
        Redis();
        Redis(const std::map<std::string, std::string> &conf);

        bool reconnect() override;
        bool connect(const std::string &ip, int port, uint64_t ms = 0) override;
        bool connect() override;
        bool setTimeout(uint64_t ms) override;
        bool isConnected() const override;
        void close() override;

        ReplyPtr cmd(const char *fmt, ...) override;
        ReplyPtr cmd(const char *fmt, va_list ap) override;
        ReplyPtr cmd(const std::vector<std::string> &argv) override;

        int appendCmd(const char *fmt, ...) override;
        int appendCmd(const char *fmt, va_list ap) override;
        int appendCmd(const std::vector<std::string> &argv) override;

        ReplyPtr getReply() override;

        /// 最近一次错误的描述(仅连接/命令失败后有意义)
        std::string getErrStr() const;

    private:
        uint32_t m_connectMs;
        struct timeval m_cmdTimeout;
        std::shared_ptr<redisContext> m_context;
        std::string m_errStr; // 最近一次错误描述(context 释放后仍可查询)
    };

} // namespace redis

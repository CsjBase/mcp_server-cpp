#include "Redis.h"
#include "Log.h"

#include <stdarg.h>
#include <stdlib.h>
#include <sys/time.h>

namespace redis
{

    static bool getConf(const std::map<std::string, std::string> &conf,
                        const std::string &key, std::string &out)
    {
        auto it = conf.find(key);
        if (it == conf.end() || it->second.empty())
            return false;
        out = it->second;
        return true;
    }

    Redis::Redis()
        : m_connectMs(3000)
    {
        m_cmdTimeout = {0, 0}; // 默认无命令超时
    }

    Redis::Redis(const std::map<std::string, std::string> &conf)
        : Redis()
    {
        std::string v;
        if (getConf(conf, "host", v) || getConf(conf, "ip", v))
            m_ip = v;
        if (getConf(conf, "port", v))
            m_port = (uint32_t)atoi(v.c_str());
        if (getConf(conf, "passwd", v))
            m_passwd = v;
        if (getConf(conf, "name", v))
            m_name = v;
        if (getConf(conf, "connect_timeout_ms", v))
            m_connectMs = (uint32_t)atoi(v.c_str());
        if (getConf(conf, "cmd_timeout_ms", v))
        {
            uint64_t ms = (uint64_t)atoi(v.c_str());
            m_cmdTimeout = {(long)(ms / 1000), (long)(ms % 1000 * 1000)};
        }
    }

    bool Redis::reconnect()
    {
        close();
        return connect();
    }

    bool Redis::isConnected() const
    {
        return m_context && !m_context->err;
    }

    void Redis::close()
    {
        m_context.reset(); // redisFree 释放连接
    }

    bool Redis::connect(const std::string &ip, int port, uint64_t ms)
    {
        m_ip = ip;
        m_port = (uint32_t)port;
        if (ms > 0)
            m_connectMs = (uint32_t)ms;
        return connect();
    }

    bool Redis::connect()
    {
        m_context.reset();

        redisContext *ctx = nullptr;
        if (m_connectMs > 0)
        {
            struct timeval tv = {(long)(m_connectMs / 1000), (long)(m_connectMs % 1000 * 1000)};
            ctx = redisConnectWithTimeout(m_ip.c_str(), (int)m_port, tv);
        }
        else
        {
            ctx = redisConnect(m_ip.c_str(), (int)m_port);
        }
        if (!ctx || ctx->err)
        {
            m_errStr = ctx ? ctx->errstr : "alloc context failed";
            REDIS_LOG_ERROR("Redis::connect() {}:{} failed: {}", m_ip, m_port, m_errStr);
            if (ctx)
                redisFree(ctx);
            return false;
        }

        m_context = std::shared_ptr<redisContext>(ctx, redisFree);
        redisSetTimeout(ctx, m_cmdTimeout);
        m_lastActiveTime = nowMs();

        // 认证
        if (!m_passwd.empty())
        {
            auto reply = cmd("AUTH %s", m_passwd.c_str());
            if (!reply || reply->type == REDIS_REPLY_ERROR)
            {
                REDIS_LOG_ERROR("Redis::connect() auth failed: {}", getErrStr());
                return false;
            }
        }
        return true;
    }

    bool Redis::setTimeout(uint64_t ms)
    {
        m_cmdTimeout = {(long)(ms / 1000), (long)(ms % 1000 * 1000)};
        if (!m_context)
            return true;
        return redisSetTimeout(m_context.get(), m_cmdTimeout) == REDIS_OK;
    }

    ReplyPtr Redis::cmd(const char *fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        ReplyPtr reply = cmd(fmt, ap);
        va_end(ap);
        return reply;
    }

    ReplyPtr Redis::cmd(const char *fmt, va_list ap)
    {
        if (!m_context)
            return nullptr;
        redisReply *reply = (redisReply *)redisvCommand(m_context.get(), fmt, ap);
        m_lastActiveTime = nowMs();
        if (!reply)
        {
            m_errStr = m_context->errstr;
            REDIS_LOG_ERROR("Redis::cmd() error: {}", m_errStr);
            return nullptr;
        }
        return ReplyPtr(reply, freeReplyObject);
    }

    ReplyPtr Redis::cmd(const std::vector<std::string> &argv)
    {
        if (!m_context)
            return nullptr;
        std::vector<const char *> args;
        args.reserve(argv.size());
        for (const auto &s : argv)
            args.push_back(s.c_str());
        redisReply *reply = (redisReply *)redisCommandArgv(m_context.get(), (int)args.size(),
                                                           args.data(), nullptr);
        m_lastActiveTime = nowMs();
        if (!reply)
        {
            m_errStr = m_context->errstr;
            REDIS_LOG_ERROR("Redis::cmd() error: {}", m_errStr);
            return nullptr;
        }
        return ReplyPtr(reply, freeReplyObject);
    }

    int Redis::appendCmd(const char *fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        int ret = appendCmd(fmt, ap);
        va_end(ap);
        return ret;
    }

    int Redis::appendCmd(const char *fmt, va_list ap)
    {
        if (!m_context)
            return REDIS_ERR;
        int ret = redisvAppendCommand(m_context.get(), fmt, ap);
        if (ret != REDIS_OK)
        {
            m_errStr = m_context->errstr;
            REDIS_LOG_ERROR("Redis::appendCmd() error: {}", m_errStr);
        }
        m_lastActiveTime = nowMs();
        return ret;
    }

    int Redis::appendCmd(const std::vector<std::string> &argv)
    {
        if (!m_context)
            return REDIS_ERR;
        std::vector<const char *> args;
        args.reserve(argv.size());
        for (const auto &s : argv)
            args.push_back(s.c_str());
        int ret = redisAppendCommandArgv(m_context.get(), (int)args.size(), args.data(), nullptr);
        if (ret != REDIS_OK)
        {
            m_errStr = m_context->errstr;
            REDIS_LOG_ERROR("Redis::appendCmd() error: {}", m_errStr);
        }
        m_lastActiveTime = nowMs();
        return ret;
    }

    ReplyPtr Redis::getReply()
    {
        if (!m_context)
            return nullptr;
        redisReply *reply = nullptr;
        if (redisGetReply(m_context.get(), (void **)&reply) != REDIS_OK || !reply)
        {
            m_errStr = m_context->errstr;
            REDIS_LOG_ERROR("Redis::getReply() error: {}", m_errStr);
            return nullptr;
        }
        m_lastActiveTime = nowMs();
        return ReplyPtr(reply, freeReplyObject);
    }

    std::string Redis::getErrStr() const
    {
        if (m_context && m_context->errstr[0] != '\0')
            return std::string(m_context->errstr);
        return m_errStr;
    }

} // namespace redis

#include "AsyncRedis.h"
#include "Log.h"

#include <stdarg.h>

namespace redis
{

    AsyncRedis::AsyncRedis(net::EventLoop *loop)
        : m_loop(loop)
    {
    }

    AsyncRedis::~AsyncRedis()
    {
        m_loop->assertInLoopThread();
        teardown();
    }

    void AsyncRedis::connect(const std::string &ip, int port,
                             ConnectCallback cb, int timeoutMs)
    {
        m_loop->assertInLoopThread();

        // 已有连接先断开(未决命令由 redisAsyncFree 统一以空 reply 回调)
        if (m_state != State::kDisconnected)
            teardown();

        m_ip = ip;
        m_port = port;
        m_connectTimeoutMs = timeoutMs;
        m_connectCallback = std::move(cb);
        ++m_generation;

        // hiredis 创建异步上下文并发起非阻塞连接(内部含阻塞 DNS 解析, 建议传 IP)
        m_ac = redisAsyncConnect(ip.c_str(), port);
        if (!m_ac)
        {
            REDIS_LOG_ERROR("AsyncRedis::connect() {}:{} redisAsyncConnect failed", ip, port);
            onConnectResult(false);
            return;
        }
        if (m_ac->err)
        {
            std::string err = m_ac->errstr ? m_ac->errstr : "unknown error";
            REDIS_LOG_ERROR("AsyncRedis::connect() {}:{} immediate fail: {}", ip, port, err);
            redisAsyncFree(m_ac); // 立即失败: 直接释放(ev 尚未注入, 安全)
            m_ac = nullptr;
            onConnectResult(false);
            return;
        }

        // 注入 csj_net 事件实现(ev.data 用于 C 回调找回 this)
        m_ac->ev.data = this;
        m_ac->ev.addRead = evAddRead;
        m_ac->ev.delRead = evDelRead;
        m_ac->ev.addWrite = evAddWrite;
        m_ac->ev.delWrite = evDelWrite;
        m_ac->ev.cleanup = evCleanup;
        // reply 所有权转交给 C++ 回调(由 ReplyPtr 释放), 与同步客户端语义一致
        m_ac->c.flags |= REDIS_NO_AUTO_FREE_REPLIES;
        redisAsyncSetConnectCallback(m_ac, onConnectCb);
        redisAsyncSetDisconnectCallback(m_ac, onDisconnectCb);

        // Channel 挂到 hiredis 的 socket 上(fd 由 hiredis 上下文持有并负责关闭)
        m_channel = std::make_unique<net::Channel>(m_loop, m_ac->c.fd);
        m_channel->setReadCallback([this](net::Timestamp t)
                                   { handleRead(t); });
        m_channel->setWriteCallback([this]()
                                    { handleWrite(); });
        m_channel->setCloseCallback([this]()
                                    { handleClose(); });
        m_channel->setErrorCallback([this]()
                                    { handleError(); });
        m_state = State::kConnecting;
        // 可写事件到达后, hiredis 在 HandleWrite 内部完成连接握手并回调 onConnect
        m_channel->enableWriting();

        // 连接超时: generation 校验防止旧定时器误伤新连接
        uint64_t gen = m_generation;
        m_loop->runAfter(timeoutMs / 1000.0, [this, gen]()
                         {
                             if (gen == m_generation && m_state == State::kConnecting)
                             {
                                 REDIS_LOG_ERROR("AsyncRedis::connect() {}:{} timeout", m_ip, m_port);
                                 teardown();
                                 onConnectResult(false);
                             }
                         });
    }

    void AsyncRedis::close()
    {
        m_loop->assertInLoopThread();
        if (m_state == State::kDisconnected)
            return;
        teardown();
    }

    void AsyncRedis::setErrorCallback(ErrorCallback cb)
    {
        m_errorCallback = std::move(cb);
    }

    // ---- 命令 ----

    void AsyncRedis::cmd(const std::vector<std::string> &argv, ReplyCallback cb)
    {
        m_loop->assertInLoopThread();
        if (m_state != State::kConnected || !m_ac)
        {
            REDIS_LOG_WARN("AsyncRedis::cmd() not connected");
            if (cb)
                cb(nullptr);
            return;
        }

        // C++ 回调打包为 privdata, 在 hiredis 回调中释放
        auto holder = std::make_unique<ReplyCallback>(std::move(cb));
        std::vector<const char *> args;
        args.reserve(argv.size());
        for (const auto &s : argv)
            args.push_back(s.c_str());

        int status = redisAsyncCommandArgv(m_ac, replyCallback, holder.get(),
                                           (int)args.size(), args.data(), nullptr);
        if (status != REDIS_OK)
        {
            REDIS_LOG_ERROR("AsyncRedis::cmd() redisAsyncCommandArgv failed");
            (*holder)(nullptr); // 失败立即回调空 reply
            return;
        }
        holder.release(); // 所有权交给 hiredis 回调
        // 写事件由 hiredis 通过 ev.addWrite 自动注册
    }

    void AsyncRedis::cmd(const char *fmt, ReplyCallback cb, ...)
    {
        m_loop->assertInLoopThread();
        if (m_state != State::kConnected || !m_ac)
        {
            REDIS_LOG_WARN("AsyncRedis::cmd() not connected");
            if (cb)
                cb(nullptr);
            return;
        }

        auto holder = std::make_unique<ReplyCallback>(std::move(cb));
        va_list ap;
        va_start(ap, cb);
        int status = redisvAsyncCommand(m_ac, replyCallback, holder.get(), fmt, ap);
        va_end(ap);
        if (status != REDIS_OK)
        {
            REDIS_LOG_ERROR("AsyncRedis::cmd() redisvAsyncCommand failed");
            (*holder)(nullptr);
            return;
        }
        holder.release();
    }

    // ---- hiredis C 回调(均在 hiredis 的 HandleRead/HandleWrite 调用栈内, 即 loop 线程) ----

    void AsyncRedis::replyCallback(redisAsyncContext *ac, void *reply, void *privdata)
    {
        std::unique_ptr<ReplyCallback> cb((ReplyCallback *)privdata);
        auto *self = (AsyncRedis *)ac->ev.data;
        self->setLastActiveTime(nowMs()); // 与同步端一致: 收到回复即刷新活跃时间
        // 已设置 REDIS_NO_AUTO_FREE_REPLIES: reply 所有权交给 ReplyPtr
        (*cb)(reply ? ReplyPtr((redisReply *)reply, freeReplyObject) : nullptr);
    }

    void AsyncRedis::onConnectCb(const redisAsyncContext *ac, int status)
    {
        AsyncRedis *self = (AsyncRedis *)ac->ev.data;
        self->onConnected(status == REDIS_OK);
    }

    void AsyncRedis::onDisconnectCb(const redisAsyncContext *ac, int status)
    {
        AsyncRedis *self = (AsyncRedis *)ac->ev.data;
        self->onDisconnected(status, ac->errstr && ac->errstr[0] ? ac->errstr
                                                                 : "connection closed by peer");
    }

    // ---- ev 钩子注入(hiredis 要求幂等) ----

    void AsyncRedis::evAddRead(void *privdata)
    {
        auto *self = (AsyncRedis *)privdata;
        if (self->m_channel)
            self->m_channel->enableReading();
    }

    void AsyncRedis::evDelRead(void *privdata)
    {
        auto *self = (AsyncRedis *)privdata;
        if (self->m_channel)
            self->m_channel->disableReading();
    }

    void AsyncRedis::evAddWrite(void *privdata)
    {
        auto *self = (AsyncRedis *)privdata;
        if (self->m_channel)
            self->m_channel->enableWriting();
    }

    void AsyncRedis::evDelWrite(void *privdata)
    {
        auto *self = (AsyncRedis *)privdata;
        if (self->m_channel)
            self->m_channel->disableWriting();
    }

    void AsyncRedis::evCleanup(void *privdata)
    {
        auto *self = (AsyncRedis *)privdata;
        // 只复位事件层; 上下文内存与 fd 由 hiredis 负责释放
        if (self->m_channel)
        {
            self->m_channel->disableAll();
            self->m_channel->remove();
            self->m_channel.reset();
        }
    }

    // ---- Channel 事件(loop 线程) ----

    void AsyncRedis::handleRead(net::Timestamp)
    {
        if (!m_ac)
            return;
        // 断连时 hiredis 内部自动清理(ev.cleanup + 未决回调空 reply + onDisconnect)
        // 并释放上下文
        redisAsyncHandleRead(m_ac);
        afterAsyncCall();
    }

    void AsyncRedis::handleWrite()
    {
        if (!m_ac)
            return;
        // 连接握手与写缓冲处理均在 hiredis 内部完成(onConnect 回调驱动状态切换)
        redisAsyncHandleWrite(m_ac);
        afterAsyncCall();
    }

    void AsyncRedis::handleClose()
    {
        if (m_state == State::kDisconnected)
            return;
        bool wasConnecting = (m_state == State::kConnecting);
        teardown(); // 未决命令由 redisAsyncFree 以空 reply 回调
        if (wasConnecting)
            onConnectResult(false); // 连接阶段失败(如对端 RST 经 EPOLLHUP 上报)
        else if (m_errorCallback)
        {
            auto cb = m_errorCallback;
            cb("connection closed");
        }
    }

    void AsyncRedis::handleError()
    {
        if (m_state == State::kDisconnected)
            return;
        bool wasConnecting = (m_state == State::kConnecting);
        teardown();
        if (wasConnecting)
            onConnectResult(false); // 连接阶段失败(如 ECONNREFUSED 经 EPOLLERR 上报)
        else if (m_errorCallback)
        {
            auto cb = m_errorCallback;
            cb("channel error");
        }
    }

    // ---- 内部状态机 ----

    void AsyncRedis::onConnected(bool ok)
    {
        if (!ok)
        {
            // 连接失败: hiredis 只回调 onConnect, 不自动释放上下文;
            // 释放由调用方在 HandleRead/HandleWrite 返回后补上(afterAsyncCall)
            REDIS_LOG_ERROR("AsyncRedis::connect() {}:{} failed: {}", m_ip, m_port,
                            m_ac && m_ac->errstr ? m_ac->errstr : "unknown error");
            m_state = State::kDisconnected;
            onConnectResult(false);
            return;
        }

        m_channel->disableWriting();
        m_state = State::kConnected;
        // 读事件由 hiredis 通过 ev.addRead 管理, 这里兜底注册一次(幂等)
        m_channel->enableReading();

        if (!m_passwd.empty())
        {
            cmd({"AUTH", m_passwd}, [this](ReplyPtr reply)
                {
                    if (reply && reply->type != REDIS_REPLY_ERROR)
                    {
                        onConnectResult(true);
                    }
                    else
                    {
                        REDIS_LOG_ERROR("AsyncRedis AUTH failed");
                        teardown();
                        onConnectResult(false);
                    }
                });
        }
        else
        {
            onConnectResult(true);
        }
    }

    void AsyncRedis::onDisconnected(int status, const char *errstr)
    {
        // 此刻 hiredis 已执行 ev.cleanup(channel 已复位)并即将释放上下文
        m_state = State::kDisconnected;
        m_ac = nullptr; // 上下文所有权归 hiredis(自动释放或 teardown 已释放)
        if (status != REDIS_OK)
        {
            REDIS_LOG_WARN("AsyncRedis connection broken: {}", errstr);
            if (m_errorCallback)
            {
                auto cb = m_errorCallback;
                cb(errstr);
            }
        }
    }

    void AsyncRedis::onConnectResult(bool ok)
    {
        auto cb = std::move(m_connectCallback);
        m_connectCallback = nullptr;
        if (cb)
            cb(ok);
    }

    /// 连接失败(onConnect ERR)不触发自动释放, 在事件处理返回后补上
    void AsyncRedis::afterAsyncCall()
    {
        if (m_ac && m_state == State::kDisconnected)
        {
            redisAsyncContext *ac = m_ac;
            m_ac = nullptr;
            redisAsyncFree(ac); // 未 CONNECTED → 不触发 onDisconnect, 安全
        }
    }

    /// 主动释放: 复位 Channel + 释放 hiredis 上下文(幂等)
    void AsyncRedis::teardown()
    {
        if (m_channel)
        {
            m_channel->disableAll();
            m_channel->remove();
            m_channel.reset();
        }
        if (m_ac)
        {
            redisAsyncContext *ac = m_ac;
            m_ac = nullptr; // 先置空: redisAsyncFree 触发的 onDisconnect 走"已释放"分支
            redisAsyncFree(ac); // 未决命令回调空 reply; 关闭 fd
        }
        m_state = State::kDisconnected;
    }

} // namespace redis

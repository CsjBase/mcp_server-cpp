#pragma once

#include "Redis.h" // IRedis + ReplyPtr
#include "net/EventLoop.h"
#include "net/Channel.h"

#include <hiredis/async.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace redis
{

    /**
     * @brief 异步命令接口(回调式, 所有操作与回调均在 EventLoop 线程)
     */
    class IAsyncRedis : public IRedis
    {
    public:
        typedef std::shared_ptr<IAsyncRedis> ptr;
        typedef std::function<void(ReplyPtr)> ReplyCallback;              // 出错时收到空 reply
        typedef std::function<void(const std::string &err)> ErrorCallback; // 连接断开
        typedef std::function<void(bool ok)> ConnectCallback;

        virtual ~IAsyncRedis() {}

        /**
         * 发起非阻塞连接; 结果通过 connCallback 在 loop 线程回调。
         * 若已存在连接会先断开。超时未完成则回调 ok=false。
         */
        virtual void connect(const std::string &ip, int port,
                             ConnectCallback cb = nullptr, int timeoutMs = 3000) = 0;
        /// 发送命令(须在 loop 线程调用); 回复按发送顺序回调
        virtual void cmd(const std::vector<std::string> &argv, ReplyCallback cb) = 0;
        /// 便捷格式版: cmd("SET %s %s", cb, key, value)
        virtual void cmd(const char *fmt, ReplyCallback cb, ...) = 0;
        virtual void setErrorCallback(ErrorCallback cb) = 0;
    };

    /**
     * @brief 异步客户端: hiredis 异步 API + csj_net EventLoop/Channel
     *
     * 实现方式(hiredis 官方适配器路线): 使用 redisAsyncContext, 将其
     * ev 结构体中的事件函数指针(addRead/delRead/addWrite/delWrite/cleanup)
     * 注入为 csj_net Channel 的 enableReading/disableReading 等实现,
     * 即"事件库适配器"模式 —— hiredis 负责连接握手、命令排队(obuf)、
     * 回复解析与分发, csj_net 负责事件循环。
     *
     * 关键语义:
     * - 连接: redisAsyncConnect 发起非阻塞连接, 可写事件到达时 hiredis 在
     *   HandleWrite 内部完成握手并回调 onConnect(成功/失败);
     *   连接失败不会自动释放上下文, 由本类在事件处理返回后补释放
     * - 断连: hiredis 检测到连接死亡后自动释放上下文(ev.cleanup 幂等),
     *   未决命令的回调收到空 ReplyPtr, 随后触发 onDisconnect → errorCallback
     * - reply 所有权: 设置 REDIS_NO_AUTO_FREE_REPLIES 后, reply 交由
     *   ReplyPtr 管理, 与同步客户端语义一致; 每条回复刷新 lastActiveTime
     * - 线程模型: 所有操作与回调均在 EventLoop 线程(assertInLoopThread),
     *   跨线程请用 loop->runInLoop/queueInLoop
     *
     * 生命周期: 与 EventLoop 同生命周期, 须在 loop 线程析构。
     */
    class AsyncRedis : public IAsyncRedis
    {
    public:
        typedef std::shared_ptr<AsyncRedis> ptr;

        explicit AsyncRedis(net::EventLoop *loop);
        ~AsyncRedis() override;

        AsyncRedis(const AsyncRedis &) = delete;
        AsyncRedis &operator=(const AsyncRedis &) = delete;

        void connect(const std::string &ip, int port,
                     ConnectCallback cb = nullptr, int timeoutMs = 3000) override;
        void cmd(const std::vector<std::string> &argv, ReplyCallback cb) override;
        void cmd(const char *fmt, ReplyCallback cb, ...) override;
        void setErrorCallback(ErrorCallback cb) override;

        bool isConnected() const override { return m_state == State::kConnected; }
        void close() override;

    private:
        enum class State
        {
            kDisconnected,
            kConnecting,
            kConnected
        };

        // ---- Channel 事件(loop 线程) ----
        void handleRead(net::Timestamp receiveTime);
        void handleWrite();
        void handleClose();
        void handleError();

        // ---- hiredis C 回调(通过 ac->ev.data 找回 this) ----
        static void onConnectCb(const redisAsyncContext *ac, int status);
        static void onDisconnectCb(const redisAsyncContext *ac, int status);
        static void replyCallback(redisAsyncContext *ac, void *reply, void *privdata);

        // ---- ev 钩子注入(hiredis 要求幂等) ----
        static void evAddRead(void *privdata);
        static void evDelRead(void *privdata);
        static void evAddWrite(void *privdata);
        static void evDelWrite(void *privdata);
        static void evCleanup(void *privdata);

        // ---- 内部状态机 ----
        void onConnected(bool ok);                       // onConnect 回调(含 AUTH)
        void onDisconnected(int status, const char *errstr); // onDisconnect 回调
        void onConnectResult(bool ok);                   // 触发 C++ ConnectCallback
        void afterAsyncCall();                           // 补释放连接失败的上下文
        void teardown();                                 // 主动释放(幂等)

        net::EventLoop *m_loop;
        net::Channel::ptr m_channel;
        redisAsyncContext *m_ac = nullptr; // hiredis 拥有 fd 与上下文内存

        State m_state = State::kDisconnected;

        ConnectCallback m_connectCallback;
        ErrorCallback m_errorCallback;

        int m_connectTimeoutMs = 3000;
        uint64_t m_generation = 0; // 连接代数, 用于使旧的超时定时器失效
    };

} // namespace redis

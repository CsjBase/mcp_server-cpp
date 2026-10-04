/**
 * 异步 Redis 客户端(AsyncRedis)端到端测试:
 * 依赖本地 redis-server(127.0.0.1:6379), 不可用时跳过。
 * EventLoop 跑在独立线程, 所有客户端操作经 runInLoop 投递,
 * 结果通过 promise/future 同步回主线程断言。
 * 对端断开场景用 CLIENT KILL(由同步连接发出)真实模拟。
 */
#include "redis/AsyncRedis.h"
#include "redis/Redis.h"

#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

using namespace redis;

namespace
{
    const char *kHost = "127.0.0.1";
    const int kPort = 6379;
} // namespace

class AsyncRedisTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // EventLoop 必须在 loop 线程构造
        std::promise<net::EventLoop *> p;
        m_loopThread = std::thread([&p, this]
                                   {
                                       m_loop = std::make_unique<net::EventLoop>();
                                       p.set_value(m_loop.get());
                                       m_loop->loop();
                                       m_loop.reset(); // 在 loop 线程析构
                                   });
        m_loopPtr = p.get_future().get();

        // 本地 redis 可用性检查
        Redis probe;
        if (!probe.connect(kHost, kPort))
            GTEST_SKIP() << "本地 redis(127.0.0.1:6379)不可用, 跳过异步客户端测试";
    }

    void TearDown() override
    {
        // 客户端须在 loop 线程析构, 且先于 quit
        m_loopPtr->runInLoop([this]()
                             { m_client.reset(); });
        m_loopPtr->runInLoop([this]()
                             { m_loopPtr->quit(); });
        m_loopThread.join();

        // 清理测试键(尽力而为)
        Redis cleaner;
        if (cleaner.connect(kHost, kPort))
        {
            cleaner.cmd("DEL csj:test:async:ak");
            cleaner.cmd("DEL csj:test:async:blockkey");
        }
    }

    /// 在 loop 线程创建客户端并发起连接, 阻塞等待连接结果
    bool connectClient(int timeoutMs = 3000)
    {
        std::promise<bool> result;
        m_loopPtr->runInLoop([&]
                             {
                                 m_client = std::make_shared<AsyncRedis>(m_loopPtr);
                                 m_client->connect(kHost, kPort,
                                                   [&result](bool ok)
                                                   { result.set_value(ok); },
                                                   timeoutMs);
                             });
        return result.get_future().get();
    }

    net::EventLoop *m_loopPtr = nullptr;
    std::unique_ptr<net::EventLoop> m_loop; // 仅 loop 线程访问
    std::thread m_loopThread;
    AsyncRedis::ptr m_client;
};

TEST_F(AsyncRedisTest, ConnectPingEchoPipeline)
{
    ASSERT_TRUE(connectClient());

    // got 由回复回调按值捕获(shared_ptr), 避免悬垂引用;
    // 写只发生在 loop 线程, 主线程经 promise 同步后读取
    auto got = std::make_shared<std::vector<std::string>>();
    std::promise<void> done;
    m_loopPtr->runInLoop([&, got]
                         {
                             auto collect = [got](ReplyPtr r)
                             {
                                 got->push_back(r ? (r->str ? r->str : "") : "<null>");
                             };
                             m_client->cmd({"PING"}, collect);
                             m_client->cmd("ECHO %s", collect, "hello");
                             m_client->cmd({"SET", "csj:test:async:ak", "av"}, collect);
                             m_client->cmd({"GET", "csj:test:async:ak"}, [got, &done](ReplyPtr r)
                                           {
                                               got->push_back(r ? (r->str ? r->str : "") : "<null>");
                                               done.set_value();
                                           });
                         });

    done.get_future().get();
    ASSERT_EQ(got->size(), 4u);
    EXPECT_EQ((*got)[0], "PONG");
    EXPECT_EQ((*got)[1], "hello");
    EXPECT_EQ((*got)[2], "OK");
    EXPECT_EQ((*got)[3], "av");
}

TEST_F(AsyncRedisTest, ConnectFail)
{
    // 找一个必然无监听的端口: bind 后立刻关闭
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(fd, (sockaddr *)&addr, sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, (sockaddr *)&addr, &len);
    int freePort = (int)ntohs(addr.sin_port);
    ::close(fd);

    std::promise<bool> result;
    m_loopPtr->runInLoop([&]
                         {
                             m_client = std::make_shared<AsyncRedis>(m_loopPtr);
                             m_client->connect("127.0.0.1", freePort,
                                               [&result](bool ok)
                                               { result.set_value(ok); },
                                               1000);
                         });
    EXPECT_FALSE(result.get_future().get());
}

TEST_F(AsyncRedisTest, PeerCloseNotifiesErrorAndFailsPending)
{
    std::promise<bool> ready;
    std::promise<bool> conn;
    std::promise<std::string> errP;
    m_loopPtr->runInLoop([&]
                         {
                             m_client = std::make_shared<AsyncRedis>(m_loopPtr);
                             m_client->setErrorCallback([&errP](const std::string &err)
                                                        { errP.set_value(err); });
                             ready.set_value(true);
                             m_client->connect(kHost, kPort,
                                               [&conn](bool ok)
                                               { conn.set_value(ok); });
                         });
    ASSERT_TRUE(ready.get_future().get());
    ASSERT_TRUE(conn.get_future().get());

    // 未决命令: BLPOP 阻塞 10s(服务端挂起该命令, 保证 kill 前无回复)
    std::promise<bool> nullP;
    m_loopPtr->runInLoop([&]
                         {
                             m_client->cmd({"BLPOP", "csj:test:async:blockkey", "10"},
                                           [&nullP](ReplyPtr r)
                                           { nullP.set_value(r == nullptr); });
                         });
    // 等命令到达服务端(本地发送为 µs 级, 100ms 足够余量)
    usleep(100 * 1000);

    // 用同步连接从服务端杀掉异步连接(仅杀其他 normal 连接, 不影响服务本身)
    {
        Redis killer;
        ASSERT_TRUE(killer.connect(kHost, kPort));
        auto reply = killer.cmd("CLIENT KILL TYPE normal SKIPME yes");
        ASSERT_TRUE(reply);
    }

    // 被杀的连接: 未决命令回调收到空 reply + errorCallback 触发
    EXPECT_TRUE(nullP.get_future().get());
    auto err = errP.get_future().get();
    EXPECT_FALSE(err.empty());
    EXPECT_FALSE(m_client->isConnected());
}

TEST_F(AsyncRedisTest, CmdWithoutConnectGetsNullReply)
{
    std::promise<bool> done;
    m_loopPtr->runInLoop([&]
                         {
                             m_client = std::make_shared<AsyncRedis>(m_loopPtr);
                             m_client->cmd({"PING"}, [&done](ReplyPtr r)
                                           { done.set_value(r == nullptr); });
                         });
    EXPECT_TRUE(done.get_future().get());
}

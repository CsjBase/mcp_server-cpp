/**
 * 同步 Redis 客户端(Redis.h)端到端测试:
 * 依赖本地 redis-server(127.0.0.1:6379), 不可用时跳过。
 * 覆盖连接/PING/SET/GET/管道/重连; 键名带 csj:test: 前缀并在用例结束清理。
 */
#include "redis/Redis.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace redis;

namespace
{
    const char *kHost = "127.0.0.1";
    const int kPort = 6379;
    const char *kKeyPrefix = "csj:test:sync:";
} // namespace

class RedisSyncTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (!m_client.connect(kHost, kPort))
            GTEST_SKIP() << "本地 redis(127.0.0.1:6379)不可用, 跳过同步客户端测试";
    }

    void TearDown() override
    {
        // 清理测试键(尽力而为)
        for (const char *k : m_keys)
            m_client.cmd("DEL %s", k);
    }

    const char *trackKey(const char *key)
    {
        m_keys.push_back(key);
        return key;
    }

    Redis m_client;
    std::vector<const char *> m_keys;
};

TEST_F(RedisSyncTest, Ping)
{
    auto reply = m_client.cmd("PING");
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->type, REDIS_REPLY_STATUS);
    EXPECT_STREQ(reply->str, "PONG");
}

TEST_F(RedisSyncTest, SetGet)
{
    const char *key = trackKey("csj:test:sync:setget");
    auto r1 = m_client.cmd("SET %s hello", key);
    ASSERT_TRUE(r1);
    EXPECT_EQ(r1->type, REDIS_REPLY_STATUS);

    auto r2 = m_client.cmd("GET %s", key);
    ASSERT_TRUE(r2);
    EXPECT_EQ(r2->type, REDIS_REPLY_STRING);
    EXPECT_STREQ(r2->str, "hello");

    // 不存在的 key
    m_client.cmd("DEL %s", key); // 确保不存在
    auto r3 = m_client.cmd("GET %s", key);
    ASSERT_TRUE(r3);
    EXPECT_EQ(r3->type, REDIS_REPLY_NIL);
}

TEST_F(RedisSyncTest, CmdByArgv)
{
    auto reply = m_client.cmd({"ECHO", "abc"});
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->type, REDIS_REPLY_STRING);
    EXPECT_STREQ(reply->str, "abc");
}

TEST_F(RedisSyncTest, Pipeline)
{
    const char *k1 = trackKey("csj:test:sync:pipe1");
    const char *k2 = trackKey("csj:test:sync:pipe2");
    ASSERT_EQ(m_client.appendCmd("SET %s v1", k1), REDIS_OK);
    ASSERT_EQ(m_client.appendCmd("SET %s v2", k2), REDIS_OK);
    ASSERT_EQ(m_client.appendCmd("GET %s", k1), REDIS_OK);
    ASSERT_EQ(m_client.appendCmd("GET %s", k2), REDIS_OK);

    auto r1 = m_client.getReply();
    auto r2 = m_client.getReply();
    auto r3 = m_client.getReply();
    auto r4 = m_client.getReply();
    ASSERT_TRUE(r1 && r2 && r3 && r4);
    EXPECT_STREQ(r1->str, "OK");
    EXPECT_STREQ(r2->str, "OK");
    EXPECT_STREQ(r3->str, "v1"); // 严格按发送顺序回复
    EXPECT_STREQ(r4->str, "v2");
}

TEST_F(RedisSyncTest, Reconnect)
{
    ASSERT_TRUE(m_client.reconnect());
    auto reply = m_client.cmd("PING");
    ASSERT_TRUE(reply);
    EXPECT_STREQ(reply->str, "PONG");
}

TEST_F(RedisSyncTest, ConnectFailed)
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

    Redis bad;
    EXPECT_FALSE(bad.connect("127.0.0.1", freePort, 100));
    EXPECT_FALSE(bad.getErrStr().empty());
}

TEST_F(RedisSyncTest, LastActiveTime)
{
    uint64_t before = m_client.getLastActiveTime();
    // 轮询直到毫秒值刷新(usleep 可能被信号提前打断, 同毫秒内比较会偶发失败)
    uint64_t after = before;
    for (int i = 0; i < 100 && after <= before; ++i)
    {
        usleep(1000);
        auto reply = m_client.cmd("PING");
        ASSERT_TRUE(reply);
        after = m_client.getLastActiveTime();
    }
    EXPECT_GT(after, before);
}

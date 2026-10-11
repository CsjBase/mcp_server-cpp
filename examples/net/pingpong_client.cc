/**
 * @file pingpong_client.cc
 * @brief net 模块 ping-pong 压测客户端：N 条连接与 echo 服务器对打。
 *
 * 设计（仿 muduo examples/pingpong）：
 * - 每条连接独占一个 EventLoop 线程（one loop per thread），互不干扰；
 * - 严格 ping-pong：连接建立后立即发第一条消息，之后每收到一个回包
 *   才发下一条，同一时刻每条连接最多一个消息在途；
 * - 全局计数用 atomic 无锁累加，主线程 loop 每秒打印一次全局统计，
 *   压测时长到点后打印汇总与每连接明细并退出。
 *
 * 用法: pingpong_client <host> <port> [connections] [message_size] [seconds]
 *   host           服务器地址，必填
 *   port           服务器端口，必填
 *   connections    并发连接数，默认 1
 *   message_size   单条消息字节数，默认 16384
 *   seconds        压测时长（秒），默认 10
 */
#include "net/EventLoop.h"
#include "net/EventLoopThread.h"
#include "net/TcpClient.h"
#include "net/base/Address.h"
#include "net/base/Log.h"
#include "net/base/Timestamp.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <inttypes.h>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    /// 单连接压测会话。roundTrips/bytes 只在其所属 IO 线程内累加，
    /// 全部线程停止后由主线程统一读取打印（join 保证 happens-before）。
    struct Session
    {
        int64_t roundTrips = 0; ///< 完成的往返次数
        int64_t bytes = 0;      ///< 累计收到的字节数
        std::string message;    ///< 每次发送的固定负载
        net::TcpClient *client = nullptr;
    };

    // 跨线程全局聚合统计（atomic 无锁累加，仅主线程读取打印）
    std::atomic<int64_t> g_roundTrips{0};
    std::atomic<int64_t> g_bytes{0};
    std::atomic<int64_t> g_startMicros{0}; // 首个连接建立时刻
    std::atomic<int> g_connected{0};       // 当前在线连接数
    std::atomic<bool> g_done{false};       // 压测到点，后续断开属于正常收尾

    void printUsage(const char *prog)
    {
        printf("Usage: %s <host> <port> [connections=1] [message_size=16384] [seconds=10]\n", prog);
    }

    double nowSeconds()
    {
        return static_cast<double>(net::Timestamp::now().microSecondsSinceEpoch()) / 1000000.0;
    }
} // namespace

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printUsage(argv[0]);
        return 1;
    }

    const std::string host = argv[1];
    const uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    const int conns = argc > 3 ? atoi(argv[3]) : 1;
    const int msgSize = argc > 4 ? atoi(argv[4]) : 16384;
    const double seconds = argc > 5 ? atof(argv[5]) : 10;
    if (conns <= 0 || msgSize <= 0 || seconds <= 0)
    {
        printUsage(argv[0]);
        return 1;
    }

    printf("pingpong client: %s:%u, %d connections, %d bytes/message, %.0f s\n",
           host.c_str(), port, conns, msgSize, seconds);

    const std::string message(static_cast<size_t>(msgSize), 'A'); // 负载内容对回显无影响

    // 主线程 loop：跑每秒统计与到点退出定时器
    net::EventLoop loop;

    std::vector<std::unique_ptr<Session>> sessions;
    sessions.reserve(conns);
    std::vector<std::unique_ptr<net::EventLoopThread>> threads;
    threads.reserve(conns);

    const auto serverAddr = std::make_shared<net::InetAddress>(host, port);

    for (int i = 0; i < conns; ++i)
    {
        auto session = std::make_unique<Session>();
        session->message = message;
        Session *raw = session.get(); // 回调在其所属 loop 线程执行，会话生命周期覆盖全程
        sessions.push_back(std::move(session));

        // 每个连接独立线程 + 独立 EventLoop + 独立 TcpClient
        auto thread = std::make_unique<net::EventLoopThread>("pingpongC" + std::to_string(i));
        net::EventLoop *clientLoop = thread->startLoop();
        threads.push_back(std::move(thread));

        raw->client = new net::TcpClient(clientLoop, serverAddr, "pingpongC" + std::to_string(i));

        // 回调在 clientLoop 线程内执行，业务无需加锁
        raw->client->setConnectionCallback([raw, msgSize](const net::TcpConnection::ptr &conn)
                                           {
                                               if (conn->connected())
                                               {
                                                   // 压测场景对延迟敏感，禁用 Nagle
                                                   conn->setTcpNoDelay(true);
                                                   // 首个连接建立时记录压测起点
                                                   int64_t zero = 0;
                                                   g_startMicros.compare_exchange_strong(
                                                       zero, net::Timestamp::now().microSecondsSinceEpoch());
                                                   g_connected.fetch_add(1);
                                                   NET_LOG_INFO("[{}] connected, start ping-pong", conn->name());
                                                   conn->send(raw->message); // 发出第一条，回包后继续
                                               }
                                               else
                                               {
                                                   g_connected.fetch_sub(1);
                                                   if (!g_done.load())
                                                   {
                                                       NET_LOG_WARN("[{}] disconnected unexpectedly", conn->name());
                                                   }
                                                   else
                                                   {
                                                       // 正常收尾的断开也记录，与服务器侧 "conn closed" 对称
                                                       NET_LOG_INFO("[{}] disconnected", conn->name());
                                                   }
                                               }
                                           });

        // 严格 ping-pong：收到回包立即再发，同一时刻最多一个消息在途
        raw->client->setMessageCallback([raw](const net::TcpConnection::ptr &conn,
                                              net::Buffer *buf, net::Timestamp)
                                        {
                                            raw->roundTrips += 1;
                                            raw->bytes += buf->readableBytes();
                                            g_roundTrips.fetch_add(1);
                                            g_bytes.fetch_add(buf->readableBytes());
                                            buf->retrieveAll();
                                            conn->send(raw->message);
                                        });

        raw->client->connect(); // Connector::start 内部 runInLoop 切到 clientLoop，跨线程安全
    }

    // 每秒打印全局统计：本秒 round trips/s、MB/s 与自起点平均
    int64_t lastTrips = 0;
    int64_t lastBytes = 0;
    loop.runEvery(1.0, [&lastTrips, &lastBytes]
                  {
                      int64_t trips = g_roundTrips.load();
                      int64_t bytes = g_bytes.load();
                      double elapsed = nowSeconds() - g_startMicros.load() / 1000000.0;
                      printf("[%8.3fs] %10" PRId64 " trips/s  %10.3f MB/s | avg %10.1f trips/s  %10.3f MB/s\n",
                             elapsed,
                             trips - lastTrips,
                             static_cast<double>(bytes - lastBytes) / (1024.0 * 1024.0),
                             elapsed > 0 ? trips / elapsed : 0,
                             elapsed > 0 ? bytes / (1024.0 * 1024.0 * elapsed) : 0);
                      lastTrips = trips;
                      lastBytes = bytes;
                  });

    // 到点打印汇总并退出；汇总时间以实际首个连接建立时刻起算
    loop.runAfter(seconds, [&loop, &sessions]
                  {
                      int64_t trips = g_roundTrips.load();
                      int64_t bytes = g_bytes.load();
                      double elapsed = nowSeconds() - g_startMicros.load() / 1000000.0;
                      printf("==== ping-pong summary ====\n");
                      printf("connections : %zu (online %d)\n", sessions.size(), g_connected.load());
                      printf("duration    : %.3f s\n", elapsed);
                      printf("round trips : %" PRId64 " total, avg %.1f trips/s\n",
                             trips, elapsed > 0 ? trips / elapsed : 0);
                      printf("throughput  : %.3f MB/s\n",
                             elapsed > 0 ? bytes / (1024.0 * 1024.0 * elapsed) : 0);
                      printf("per-connection:\n");
                      for (size_t i = 0; i < sessions.size(); ++i)
                      {
                          printf("  conn %-3zu: %" PRId64 " trips, %.1f trips/s, %.3f MB/s\n",
                                 i, sessions[i]->roundTrips,
                                 elapsed > 0 ? sessions[i]->roundTrips / elapsed : 0,
                                 elapsed > 0 ? sessions[i]->bytes / (1024.0 * 1024.0 * elapsed) : 0);
                      }
                      loop.quit();
                      g_done.store(true);
                  });

    loop.loop();

    // 优雅断开全部连接：shutdown 经 runInLoop 入队，随后线程析构 quit+join
    // 时 pending functor 仍会被执行，服务器侧收到干净 FIN 而非 RST
    for (const auto &session : sessions)
    {
        session->client->disconnect();
    }
    // 等待断开回调真正执行完（handleClose → 日志）再停线程。
    // 若立即 quit，loop 不会再 poll，服务端回的 FIN 不会被处理，断开日志会丢失
    for (int i = 0; i < 100 && g_connected.load() > 0; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // 停止全部客户端线程：EventLoopThread 析构 quit + join，保证会话数据可安全读取
    threads.clear();
    printf("pingpong client exit\n");
    return 0;
}

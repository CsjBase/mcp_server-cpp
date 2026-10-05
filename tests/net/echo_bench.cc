/**
 * @file echo_bench.cc
 * @brief TCP echo 吞吐基准: 自研 TcpServer 1 线程 vs 4 线程(Round-Robin)
 *
 * 服务端: net::TcpServer 在独立线程运行(栈对象, loop 退出后线程内析构);
 * 客户端: 原生阻塞 socket, 每客户端线程 1 连接, 串行 send/recv 固定
 * 大小消息统计往返(RTT)吞吐 —— 压测工具视角, 不引入自研客户端。
 * 输出: 总 QPS(RTT/s)、平均 RT(us)。
 */
#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "net/base/Address.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
    constexpr int kPayloadSize = 64;
    constexpr int kItersPerConn = 100'000;
    constexpr int kWarmupIters = 2'000;

    inline double nowSec()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    /// 服务端封装: 独立线程跑 EventLoop, TcpServer 为该线程栈对象。
    /// EventLoop 必须在其所属线程内构造(记录 m_threadId), 故在线程内创建,
    /// 用 promise 同步给主线程用于 stop()。
    struct EchoServerCtx
    {
        net::EventLoop *loop;
        std::thread th;
        std::promise<void> loopReady;

        void start(int ioThreads, uint16_t port)
        {
            th = std::thread([this, ioThreads, port]
                             {
                                 net::EventLoop l;
                                 net::TcpServer server(&l,
                                                       std::make_shared<net::InetAddress>(port),
                                                       "echo_bench");
                                 // 连接回调不可缺省: connectEstablished 会无条件调用
                                 server.setConnectionCallback(
                                     [](const net::TcpConnection::ptr &) {});
                                 server.setMessageCallback(
                                     [](const net::TcpConnection::ptr &conn,
                                        net::Buffer *buf, net::Timestamp)
                                     {
                                         conn->send(buf->retrieveAllAsString());
                                     });
                                 server.setThreadNum(ioThreads);
                                 server.start();

                                 loop = &l;
                                 loopReady.set_value();
                                 l.loop();
                                 // loop 退出后 server 在此线程析构
                             });
            loopReady.get_future().get();
        }

        void stop()
        {
            // loop->runInLoop([l = loop] { l->quit(); });
            loop->quit();
            th.join();
        }
    };

    /// 连接(重试直到成功, 服务端可能尚未 listen)
    int connectWithRetry(uint16_t port, int maxRetries)
    {
        for (int i = 0; i < maxRetries; ++i)
        {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            if (::connect(fd, (sockaddr *)&addr, sizeof(addr)) == 0)
                return fd;
            ::close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return -1;
    }

    /// 单连接串行往返: 返回 QPS 与平均 RT(us); 失败返回 -1
    double runConn(uint16_t port, int iters, double &rtUs)
    {
        int fd = connectWithRetry(port, 40);
        if (fd < 0)
            return -1;

        std::string payload(kPayloadSize, 'x');
        std::vector<char> recvBuf(kPayloadSize);

        auto sendAll = [&]() -> bool
        {
            ssize_t off = 0;
            while (off < kPayloadSize)
            {
                ssize_t n = ::send(fd, payload.data() + off, kPayloadSize - off, MSG_NOSIGNAL);
                if (n <= 0)
                    return false;
                off += n;
            }
            return true;
        };
        auto recvAll = [&]() -> bool
        {
            ssize_t off = 0;
            while (off < kPayloadSize)
            {
                ssize_t n = ::recv(fd, recvBuf.data() + off, kPayloadSize - off, 0);
                if (n <= 0)
                    return false;
                off += n;
            }
            return true;
        };

        double t0 = nowSec();
        for (int i = 0; i < iters; ++i)
        {
            if (!sendAll() || !recvAll())
            {
                ::close(fd);
                return -1;
            }
        }
        double sec = nowSec() - t0;
        ::close(fd);

        rtUs = sec / iters * 1e6;
        return (double)iters / sec;
    }

    void runScenario(int ioThreads, int clientThreads, uint16_t port)
    {
        EchoServerCtx srv;
        srv.start(ioThreads, port);

        // 预热: 1 连接少量往返, 排除 listen/首包冷启动
        {
            double rt;
            runConn(port, kWarmupIters, rt);
        }

        std::vector<std::thread> clients;
        std::vector<double> qps(clientThreads, 0);
        std::vector<double> rt(clientThreads, 0);
        std::vector<int> ok(clientThreads, 1);

        double t0 = nowSec();
        for (int t = 0; t < clientThreads; ++t)
        {
            clients.emplace_back([&, t]
                                 {
                                     double r = runConn(port, kItersPerConn, rt[t]);
                                     if (r < 0)
                                         ok[t] = 0;
                                     else
                                         qps[t] = r; });
        }
        for (auto &c : clients)
            c.join();
        double wall = nowSec() - t0;

        double totalQps = 0, avgRt = 0;
        int okCount = 0;
        for (int t = 0; t < clientThreads; ++t)
        {
            if (ok[t])
            {
                totalQps += qps[t];
                avgRt += rt[t];
                ++okCount;
            }
        }
        avgRt = okCount ? avgRt / okCount : 0;

        std::cout << std::left << std::setw(10) << ioThreads
                  << std::setw(20) << clientThreads
                  << std::right << std::setw(12) << (long long)totalQps
                  << std::setw(10) << (long long)(avgRt * 1000) << " ns"
                  << std::setw(8) << (int)(wall * 1000) << " ms"
                  << (okCount == clientThreads ? "" : "  [有连接失败!]") << "\n";

        srv.stop();
    }

} // namespace

int main()
{
    std::cout << "负载: " << kPayloadSize << "B 往返, 每连接 " << kItersPerConn
              << " 次 RTT, 预热 " << kWarmupIters << " 次\n\n";
    std::cout << std::left << std::setw(10) << "IO线程"
              << std::setw(20) << "客户端线程"
              << std::right << std::setw(12) << "总QPS(RTT/s)"
              << std::setw(14) << "平均RT"
              << std::setw(12) << "压测时长\n";
    std::cout << std::string(58, '-') << "\n";

    runScenario(1, 4, 9101);
    runScenario(4, 4, 9102);
    return 0;
}

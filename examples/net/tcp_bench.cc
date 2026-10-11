/**
 * @file tcp_bench.cc
 * @brief net 模块 TCP 压测工具：并发连接数 / QPS / P99 延迟 / 错误率。
 *
 * 两种模式（服务器端复用 pingpong_server 的 echo 能力）：
 *
 * 1. QPS/延迟/错误率（默认模式）：
 *    conns 条连接分布在 threads 个 EventLoop 线程上（多连接共享一个 loop，
 *    避免"一连接一线程"的线程开销）。每条连接维持 window 条消息在途
 *    （流水线）：收到回包立即补发，吞吐由 CPU/带宽决定而非 RTT；
 *    window=1 即严格 ping-pong，测纯 RTT。
 *    - 消息格式：前 8 字节为本连接递增 seq，其余为填充。回包逐块校验
 *      seq 与长度，不符计错误，错误率 = 错误块数 / 总回包数。
 *    - 延迟：发送时刻入 FIFO，回包到达按序弹出（TCP 有序保证对应正确），
 *      RTT 记入 1us 粒度直方图，结束时合并全部连接算 P50/P90/P99/P99.9。
 *
 * 2. 并发连接规模（--conntest N）：
 *    N 个 TcpClient 均布到 threads 个 loop，建立后保持空闲在线。
 *    统计超时窗口内成功建立数、建立耗时、客户端进程 RSS 增量
 *    （近似每连接内存占用）。
 *    注意 Connector 失败会无限重试，故"失败数"= 超时窗口内未建立数。
 *
 * 用法:
 *   tcp_bench <host> <port> [-c conns=1] [-t threads=1] [-s msg_size=1024]
 *             [-d seconds=10] [-w window=1] [--conntest N] [--connect-timeout s=60]
 */
#include "net/EventLoop.h"
#include "net/EventLoopThread.h"
#include "net/TcpClient.h"
#include "net/base/Address.h"
#include "net/base/Log.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <getopt.h>
#include <inttypes.h>
#include <sys/resource.h>
#include <memory>
#include <string>
#include <vector>

namespace
{
    constexpr int kSeqBytes = 8;       ///< 消息头 seq 长度
    constexpr int kHistBuckets = 65536; ///< 直方图桶数，每桶 1us，覆盖 0~65ms。
                                        ///< 窗口>1 时消息在途排队，RTT≈窗口×服务间隔，需宽覆盖
    constexpr int kOverflow = kHistBuckets;

    /// 稳态时钟微秒（测延迟不受墙钟调整影响）
    int64_t steadyUs()
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    /// 1us 粒度固定桶直方图：O(1) 记录，O(桶数) 算分位
    struct Histogram
    {
        int64_t buckets[kHistBuckets + 1] = {0};

        void add(int64_t us)
        {
            if (us >= 0 && us < kHistBuckets)
                ++buckets[us];
            else
                ++buckets[kOverflow];
        }

        void merge(const Histogram &other)
        {
            for (int i = 0; i <= kHistBuckets; ++i)
                buckets[i] += other.buckets[i];
        }

        int64_t total() const
        {
            int64_t t = 0;
            for (int i = 0; i <= kHistBuckets; ++i)
                t += buckets[i];
            return t;
        }

        /// p 为百分数（如 99.9），返回对应分位延迟（us）
        int64_t percentile(double p) const
        {
            int64_t n = total();
            if (n == 0)
                return 0;
            int64_t target = static_cast<int64_t>(std::ceil(p * n / 100.0));
            if (target < 1)
                target = 1;
            int64_t acc = 0;
            for (int i = 0; i <= kHistBuckets; ++i)
            {
                acc += buckets[i];
                if (acc >= target)
                    return i;
            }
            return kHistBuckets;
        }

        double mean() const
        {
            int64_t n = total();
            if (n == 0)
                return 0;
            double sum = 0;
            for (int i = 0; i <= kHistBuckets; ++i)
                sum += static_cast<double>(buckets[i]) * i;
            return sum / n;
        }
    };

    /// 单连接会话：除 client 指针外，其余字段只在所属 loop 线程内读写，
    /// 全部线程 join 后主线程合并统计（happens-before 保证安全）。
    struct Session
    {
        net::TcpClient *client = nullptr;
        std::deque<int64_t> inflight; ///< 在途消息发送时刻 FIFO（us）
        int64_t sent = 0;             ///< 已发送消息数，也是下一条消息的 seq
        int64_t completed = 0;        ///< 已消费回包数，即期望的下一个 seq
        int64_t msgErrors = 0;        ///< 本连接消息校验错误（块级）
        bool everConnected = false;
        Histogram hist;               ///< RTT 直方图
        std::string message;          ///< 发送模板，前 kSeqBytes 字节写 seq
    };

    // 跨线程全局统计（atomic 无锁累加，主线程读取）
    std::atomic<int64_t> g_connectOk{0};
    std::atomic<int64_t> g_disconnects{0}; ///< 压测期间异常断开数
    std::atomic<int64_t> g_completed{0};   ///< 校验通过的回包总数
    std::atomic<int64_t> g_bytes{0};       ///< 回包总字节数
    std::atomic<int64_t> g_msgErrors{0};   ///< 消息校验错误总数
    std::atomic<int64_t> g_startUs{0};     ///< 首个连接建立时刻（us）
    std::atomic<bool> g_done{false};       ///< 压测到点，此后断开属正常收尾

    /// 尽力调大 fd 上限：连接数多时默认 1024 会不够，失败仅告警不中断
    void raiseFdLimit(int needFds)
    {
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
            return;
        rlim_t want = static_cast<rlim_t>(needFds) + 64;
        if (rl.rlim_cur >= want)
            return;
        rl.rlim_cur = std::min<rlim_t>(want, rl.rlim_max);
        if (setrlimit(RLIMIT_NOFILE, &rl) != 0)
        {
            printf("[warn] setrlimit(RLIMIT_NOFILE) failed, 连接数超过 %ld 时可能失败，请先 ulimit -n\n",
                   static_cast<long>(rl.rlim_cur));
        }
    }

    /// 读取当前进程 RSS（KB），压测前后采样算内存增量
    int64_t readVmRssKb()
    {
        std::ifstream f("/proc/self/status");
        std::string line;
        while (std::getline(f, line))
        {
            if (line.rfind("VmRSS:", 0) == 0)
                return atoll(line.c_str() + 6);
        }
        return -1;
    }

    /// 向指定连接补发 k 条消息，维持流水线窗口。
    /// 窗口内连发耗时远小于 1us 级测量粒度，故共用同一发送时刻。
    void sendWindow(Session *raw, const net::TcpConnection::ptr &conn, int64_t k)
    {
        int64_t now = steadyUs();
        for (int64_t i = 0; i < k; ++i)
        {
            int64_t seq = raw->sent++;
            memcpy(&raw->message[0], &seq, kSeqBytes);
            raw->inflight.push_back(now);
            conn->send(raw->message);
        }
    }

    void printUsage(const char *prog)
    {
        printf("Usage: %s <host> <port> [-c conns=1] [-t threads=1] [-s msg_size=1024]\n"
               "          [-d seconds=10] [-w window=1] [--conntest N] [--connect-timeout s=60]\n"
               "  -c              并发连接数\n"
               "  -t              EventLoop 线程数（连接均布其上，多连接共享 loop）\n"
               "  -s              单条消息字节数（>= 16，前 8 字节为 seq）\n"
               "  -d              压测时长（秒）\n"
               "  -w              每连接在途窗口：1=严格 ping-pong 测 RTT，>1=流水线打吞吐\n"
               "  --conntest N    连接规模模式：建立 N 条空闲连接并保持\n"
               "  --connect-timeout  建立超时（秒）：压测模式默认 5（超时无连接则报错退出），\n"
               "                     连接规模模式默认 60\n",
               prog);
    }

    struct Options
    {
        std::string host;
        int port = 0;
        int conns = 1;
        int threads = 1;
        int msgSize = 1024;
        double duration = 10;
        int window = 1;
        int conntest = 0;           ///< >0 表示连接规模模式及目标连接数
        double connectTimeout = 0;  ///< 0 = 未设置：conntest 模式默认 60s，压测模式默认 5s
    };

    bool parseArgs(int argc, char *argv[], Options &opt)
    {
        if (argc < 3)
            return false;
        opt.host = argv[1];
        opt.port = atoi(argv[2]);

        static option longOpts[] = {
            {"conntest", required_argument, nullptr, 1000},
            {"connect-timeout", required_argument, nullptr, 1001},
            {nullptr, 0, nullptr, 0},
        };
        int c;
        while ((c = getopt_long(argc, argv, "c:t:s:d:w:h", longOpts, nullptr)) != -1)
        {
            switch (c)
            {
            case 'c':
                opt.conns = atoi(optarg);
                break;
            case 't':
                opt.threads = atoi(optarg);
                break;
            case 's':
                opt.msgSize = atoi(optarg);
                break;
            case 'd':
                opt.duration = atof(optarg);
                break;
            case 'w':
                opt.window = atoi(optarg);
                break;
            case 1000:
                opt.conntest = atoi(optarg);
                break;
            case 1001:
                opt.connectTimeout = atof(optarg);
                break;
            default:
                return false;
            }
        }
        return opt.port > 0 && opt.conns > 0 && opt.threads > 0 && opt.msgSize >= kSeqBytes + 8 &&
               opt.duration > 0 && opt.window > 0;
    }

    /// 启动 numThreads 个 EventLoop 线程。
    /// 注意 startLoop() 每线程只能调用一次（promise 只能取一次 future），
    /// 故循环指针缓存到 loops 供后续连接复用。
    void spawnLoops(int numThreads, std::vector<std::unique_ptr<net::EventLoopThread>> &threads,
                    std::vector<net::EventLoop *> &loops)
    {
        threads.reserve(numThreads);
        loops.reserve(numThreads);
        for (int i = 0; i < numThreads; ++i)
        {
            auto t = std::make_unique<net::EventLoopThread>("benchT" + std::to_string(i));
            loops.push_back(t->startLoop());
            threads.push_back(std::move(t));
        }
    }

    /**
     * @brief 默认模式：QPS / 延迟分位 / 错误率
     */
    int runBench(const Options &opt)
    {
        // 建立超时：Connector 失败会无限重试，无此保护时服务器未启动会静默挂满时长
        double establishTimeout = opt.connectTimeout > 0 ? opt.connectTimeout : 5.0;
        printf("tcp_bench: %s:%d, %d conns on %d loops, %d B/msg, window %d, %.0f s (establish timeout %.0f s)\n",
               opt.host.c_str(), opt.port, opt.conns, opt.threads, opt.msgSize, opt.window, opt.duration,
               establishTimeout);

        net::EventLoop loop; // 主线程 loop：统计定时器
        std::vector<std::unique_ptr<net::EventLoopThread>> threads;
        std::vector<net::EventLoop *> loops;
        spawnLoops(opt.threads, threads, loops);
        const auto serverAddr = std::make_shared<net::InetAddress>(opt.host, opt.port);

        std::vector<std::unique_ptr<Session>> sessions;
        sessions.reserve(opt.conns);
        for (int i = 0; i < opt.conns; ++i)
        {
            auto raw = std::make_unique<Session>();
            raw->message.assign(opt.msgSize, 'A');
            Session *p = raw.get();
            sessions.push_back(std::move(raw));

            net::EventLoop *clientLoop = loops[i % opt.threads];
            p->client = new net::TcpClient(clientLoop, serverAddr, "benchC" + std::to_string(i));

            // 回调在 clientLoop 线程执行，会话字段无锁访问
            p->client->setConnectionCallback([p, &opt](const net::TcpConnection::ptr &conn)
                                             {
                                                 if (conn->connected())
                                                 {
                                                     // 测延迟/QPS 需禁用 Nagle（conntest 模式不设，空闲连接无影响）
                                                     conn->setTcpNoDelay(true);
                                                     p->everConnected = true;
                                                     if (g_connectOk.fetch_add(1) == 0)
                                                     {
                                                         g_startUs.store(steadyUs()); // 首个连接为压测起点
                                                     }
                                                     sendWindow(p, conn, opt.window);
                                                 }
                                                 else
                                                 {
                                                     // 只统计压测期间的异常断开，到点后的优雅收尾不计
                                                     if (p->everConnected && !g_done.load())
                                                     {
                                                         g_disconnects.fetch_add(1);
                                                         NET_LOG_WARN("[{}] disconnected unexpectedly", conn->name());
                                                     }
                                                 }
                                             });

            // 回包处理：按 msgSize 切块，逐块校验 seq；每块弹出一个在途时间戳算 RTT。
            // 注意 TCP 可能只读到半块：只消费整块，余量留在 Buffer 内等下次拼齐，
            // 丢弃会错位（字节流按块对齐，丢半块后所有 seq 都会失配）。
            p->client->setMessageCallback([p, &opt](const net::TcpConnection::ptr &conn,
                                                    net::Buffer *buf, net::Timestamp)
                                          {
                                              size_t n = buf->readableBytes();
                                              size_t k = n / opt.msgSize;
                                              if (k > p->inflight.size())
                                              {
                                                  // 回包多于在途数：协议错乱，按在途数截断
                                                  ++p->msgErrors;
                                                  g_msgErrors.fetch_add(1);
                                                  k = p->inflight.size();
                                              }
                                              int64_t now = steadyUs();
                                              for (size_t i = 0; i < k; ++i)
                                              {
                                                  int64_t seq;
                                                  memcpy(&seq, buf->peek() + i * opt.msgSize, kSeqBytes);
                                                  if (seq != p->completed)
                                                  {
                                                      ++p->msgErrors;
                                                      g_msgErrors.fetch_add(1);
                                                  }
                                                  ++p->completed;
                                                  p->hist.add(now - p->inflight.front());
                                                  p->inflight.pop_front();
                                              }
                                              size_t consumed = k * opt.msgSize;
                                              buf->retrieve(consumed);
                                              if (k > 0)
                                              {
                                                  g_completed.fetch_add(k);
                                                  g_bytes.fetch_add(consumed);
                                                  sendWindow(p, conn, k); // 补发维持窗口
                                              }
                                          });

            p->client->connect();
        }

        int64_t lastCompleted = 0;
        int64_t lastBytes = 0;
        loop.runEvery(1.0, [&lastCompleted, &lastBytes]
                      {
                          int64_t done = g_completed.load();
                          int64_t bytes = g_bytes.load();
                          int64_t startUs = g_startUs.load();
                          double elapsed = startUs > 0 ? (steadyUs() - startUs) / 1e6 : 0;
                          if (startUs > 0)
                          {
                              printf("[%8.3fs] QPS: %10" PRId64 " | %.3f MB/s | online: %" PRId64
                                     " | errors: %" PRId64 " | avg QPS: %.0f\n",
                                     elapsed,
                                     done - lastCompleted,
                                     (bytes - lastBytes) / (1024.0 * 1024.0),
                                     g_connectOk.load() - g_disconnects.load(),
                                     g_msgErrors.load(),
                                     elapsed > 0 ? done / elapsed : 0);
                          }
                          else
                          {
                              printf("[connect] waiting for first connection...\n");
                          }
                          lastCompleted = done;
                          lastBytes = bytes;
                      });
        // 建立超时保护：超时后仍无任何连接建立，判定服务器不可达并报错退出
        loop.runAfter(establishTimeout, [&loop, &opt, establishTimeout]
                      {
                          if (g_connectOk.load() == 0)
                          {
                              printf("[error] %s:%d unreachable: no connection established in %.0f s\n",
                                     opt.host.c_str(), opt.port, establishTimeout);
                              printf("[error] 请确认服务器已启动，如: ./bin/examples/net/pingpong_server %d\n",
                                     opt.port);
                              loop.quit();
                              g_done.store(true);
                          }
                      });
        loop.runAfter(opt.duration, [&loop]
                      {
                          loop.quit();
                          g_done.store(true);
                      });
        loop.loop();

        // 优雅断开 → 线程退出 join → 之后会话数据可安全读取
        for (const auto &s : sessions)
            s->client->disconnect();
        threads.clear();

        int64_t done = g_completed.load();
        int64_t bytes = g_bytes.load();
        int64_t errors = g_msgErrors.load();
        int64_t startUs = g_startUs.load();
        double elapsed = startUs > 0 ? (steadyUs() - startUs) / 1e6 : 0;

        Histogram merged;
        for (const auto &s : sessions)
            merged.merge(s->hist);

        if (g_connectOk.load() == 0)
        {
            printf("==== tcp_bench FAILED: no connection established to %s:%d ====\n",
                   opt.host.c_str(), opt.port);
            return 1;
        }

        printf("==== tcp_bench summary ====\n");
        printf("connections   : %d (established %" PRId64 ", unexpected disconnects %" PRId64 ")\n",
               opt.conns, g_connectOk.load(), g_disconnects.load());
        printf("duration      : %.3f s\n", elapsed);
        printf("QPS           : %.0f req/s\n", elapsed > 0 ? done / elapsed : 0);
        printf("bandwidth     : %.3f MB/s\n", elapsed > 0 ? bytes / (1024.0 * 1024.0 * elapsed) : 0);
        printf("latency       : mean %.1f us | P50 %" PRId64 " us | P90 %" PRId64 " us | "
               "P99 %" PRId64 " us | P99.9 %" PRId64 " us | max %" PRId64 " us\n",
               merged.mean(), merged.percentile(50), merged.percentile(90),
               merged.percentile(99), merged.percentile(99.9), merged.percentile(100));
        printf("error rate    : %" PRId64 " / %" PRId64 " = %.4f%%\n",
               errors, done, done > 0 ? 100.0 * errors / done : 0);
        return 0;
    }

    /**
     * @brief 连接规模模式：建立 N 条空闲连接并保持，统计成功数与内存开销。
     *        Connector 失败会无限重试，故以超时窗口内成功建立数为准。
     */
    int runConntest(const Options &opt)
    {
        int n = opt.conntest;
        double timeout = opt.connectTimeout > 0 ? opt.connectTimeout : 60.0;
        printf("conntest: %s:%d, target %d conns on %d loops, timeout %.0f s\n",
               opt.host.c_str(), opt.port, n, opt.threads, timeout);

        int64_t rssBefore = readVmRssKb();
        int64_t t0 = steadyUs();

        net::EventLoop loop;
        std::vector<std::unique_ptr<net::EventLoopThread>> threads;
        std::vector<net::EventLoop *> loops;
        spawnLoops(opt.threads, threads, loops);
        const auto serverAddr = std::make_shared<net::InetAddress>(opt.host, opt.port);

        // N 个客户端均布到 threads 个 loop；连接成功后保持空闲在线
        std::vector<std::unique_ptr<net::TcpClient>> clients;
        clients.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            net::EventLoop *clientLoop = loops[i % opt.threads];
            auto client = std::make_unique<net::TcpClient>(clientLoop, serverAddr, "connC" + std::to_string(i));
            client->setConnectionCallback([](const net::TcpConnection::ptr &conn)
                                          {
                                              if (conn->connected())
                                                  g_connectOk.fetch_add(1);
                                          });
            client->connect();
            clients.push_back(std::move(client));
        }

        // 每秒打印建立进度；全部建立或超时后退出
        loop.runEvery(1.0, [n, t0, &loop]
                      {
                          double elapsed = (steadyUs() - t0) / 1e6;
                          printf("[%6.2fs] established %" PRId64 " / %d\n", elapsed, g_connectOk.load(), n);
                          if (g_connectOk.load() >= n)
                              loop.quit();
                      });
        loop.runAfter(timeout, [&loop]
                      { loop.quit(); });
        loop.loop();

        double elapsed = (steadyUs() - t0) / 1e6;
        int64_t ok = g_connectOk.load();
        int64_t rssAfter = readVmRssKb();
        double rssDeltaKb = static_cast<double>(rssAfter - rssBefore);

        printf("==== conntest summary ====\n");
        printf("target        : %d\n", n);
        printf("established   : %" PRId64 " (fail/timeout %" PRId64 ")\n", ok, n - ok);
        printf("elapsed       : %.2f s\n", elapsed);
        printf("client RSS    : +%.1f MB (%.1f KB/conn)\n",
               rssDeltaKb / 1024.0, ok > 0 ? rssDeltaKb / ok : 0);
        return 0;
    }
} // namespace

int main(int argc, char *argv[])
{
    Options opt;
    if (!parseArgs(argc, argv, opt))
    {
        printUsage(argv[0]);
        return 1;
    }
    raiseFdLimit(opt.conntest > 0 ? opt.conntest : opt.conns);
    return opt.conntest > 0 ? runConntest(opt) : runBench(opt);
}

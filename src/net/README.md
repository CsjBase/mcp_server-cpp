# net — Reactor 网络库

仿 muduo 架构、从零实现的 epoll + 非阻塞 I/O 网络库。核心思想是 **one loop per thread**：每个线程一个 `EventLoop`，所有 I/O 事件与回调都在所属线程内执行，跨线程交互通过唤醒队列（eventfd）完成，业务代码无需加锁。上层提供 `TcpServer` / `TcpClient` 与 HTTP 服务框架。

## 目录结构

```
net/
├── EventLoop.h / .cc        # 事件循环：epoll 等待、pending functor、定时器、唤醒
├── Poller.h / .cc           # epoll 封装（update/remove/poll）
├── Channel.h / .cc          # fd + 事件回调（READ/WRITE/CLOSE/ERROR），不拥有 fd
├── Timer.h / TimerId.h      # 定时器与句柄
├── TimerManager.h / .cc     # 定时器管理（set + 最早到期时间驱动 poll 超时）
├── Acceptor.h / .cc         # 监听 socket，接受新连接
├── Connector.h / .cc        # 主动连接（TcpClient 使用，支持重试）
├── TcpServer.h / .cc        # 服务器：Round-Robin 分配连接到 IO 线程池
├── TcpClient.h / .cc        # 客户端
├── TcpConnection.h / .cc    # 一条连接：读缓冲、写缓冲、生命周期回调
├── Buffer.h / .cc           # 应用层读写缓冲（prependable + readable 结构）
├── EventLoopThread.h / .cc  # 一个线程跑一个 EventLoop
├── EventLoopThreadPool.h    # IO 线程池（Round-Robin 取下一个 loop）
├── CallBacks.h              # 各类回调类型定义
├── base/                    # Address/Socket/SocketOps/Thread/Timestamp/endian
└── http/                    # HTTP 协议解析 + HttpServer + Servlet 分发
```

## 整体架构

```
                        ┌──────────────────────────────┐
                        │  main thread                  │
                        │  EventLoop（Acceptor 所在）    │
                        └──────────────┬───────────────┘
                                       │ 新连接，Round-Robin
              ┌────────────────────────┼────────────────────────┐
              ▼                        ▼                        ▼
     ┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
     │ IO thread 1     │      │ IO thread 2     │      │ IO thread N     │
     │ EventLoop       │      │ EventLoop       │      │ EventLoop       │
     │  ├ Poller(epoll)│      │  ├ Poller(epoll)│      │  ├ Poller(epoll)│
     │  ├ TimerManager │      │  ├ TimerManager │      │  ├ TimerManager │
     │  └ TcpConnections      │  └ TcpConnections      │  └ TcpConnections
     └─────────────────┘      └─────────────────┘      └─────────────────┘
```

- `EventLoop::loop()`：`Poller::poll(timeout)` → 分发活动 Channel 事件 → 执行 `doPendingFunctors()`。
- 跨线程调 `runInLoop` / `queueInLoop`：目标线程不在本线程时入队并通过 eventfd `wakeup()`。
- 连接断开统一走 `removeConnectionInLoop`，连接对象在其所属 loop 内析构。

## 快速上手：TCP 服务器

```cpp
#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "net/base/Address.h"

net::EventLoop loop;

auto addr = std::make_shared<net::InetAddress>("0.0.0.0", 8080);
net::TcpServer server(&loop, addr, "echo");

server.setThreadNum(4);   // 4 个 IO 线程
server.setConnectionCallback([](const net::TcpConnection::ptr &conn) {
    if (conn->connected()) {
        conn->setTcpNoDelay(true);   // 低延迟场景禁用 Nagle
        LOG_INFO(LOGGER_DEFAULT(), "new conn: {}", conn->name());
    } else {
        LOG_INFO(LOGGER_DEFAULT(), "conn closed: {}", conn->name());
    }
});
server.setMessageCallback([](const net::TcpConnection::ptr &conn,
                             net::Buffer *buf, net::Timestamp t) {
    // echo：原样发回
    conn->send(buf->retrieveAllAsString());
});

server.start();
loop.loop();
```

### 关键回调

| 回调 | 触发时机 |
|---|---|
| `ConnectionCallback` | 连接建立 / 断开 |
| `MessageCallback` | 读到新数据（`Buffer` 中为本次可读内容） |
| `WriteCompleteCallback` | 写缓冲全部发完（可做流量控制） |
| `ThreadInitCallback` | IO 线程启动时（在该线程内执行，可做线程局部初始化） |

## TCP 客户端

```cpp
net::EventLoop loop;
auto serverAddr = std::make_shared<net::InetAddress>("127.0.0.1", 8080);
net::TcpClient client(&loop, serverAddr, "client");

client.enableRetry();   // 连接失败自动重试
client.setConnectionCallback([&](const net::TcpConnection::ptr &conn) {
    if (conn->connected()) {
        conn->send("hello");
    } else {
        loop.quit();
    }
});
client.connect();
loop.loop();
```

## 定时器

```cpp
loop.runAfter(3.0, [] { LOG_INFO(LOGGER_DEFAULT(), "3 秒后执行一次"); });
net::TimerId t = loop.runEvery(1.0, [] { /* 每秒心跳 */ });
loop.runAt(net::Timestamp::now() + 10, [] { /* 定点执行 */ });
loop.cancel(t);   // 跨线程安全
```

## HTTP 服务

```cpp
#include "net/http/HttpServer.h"
#include "net/http/Servlet.h"

net::EventLoop loop;
auto addr = std::make_shared<net::InetAddress>("0.0.0.0", 8080);

net::http::HttpServer server(/*keepalive=*/true, &loop, addr, "http");
auto dispatch = std::make_shared<net::http::ServletDispatch>();

// 函数式 Servlet：匹配 /hello
dispatch->addServlet("/hello", [](net::http::HttpRequest::ptr req,
                                  net::http::HttpResponse::ptr rsp) {
    rsp->setStatus(net::http::HttpStatus::OK);
    rsp->setBody("<h1>Hello McpServer</h1>");
    return 0;
});

// 类式 Servlet：按需创建实例
dispatch->addServletCreator<MyUserServlet>("/user");

server.setServletDispatch(dispatch);
server.start(/*numThreads=*/4);
loop.loop();
```

HTTP 协议栈（`net/http/`）自带请求/响应解析器（基于 http-parser 风格的状态机）、`HttpRequest` / `HttpResponse` 对象模型、keep-alive 支持，`ServletDispatch` 按 URI 精确匹配分发。

## 性能基准：ping-pong

`examples/net/` 提供仿 muduo 的 ping-pong 压测：客户端与 echo 服务器对打，统计每秒往返次数（round trips/s）与吞吐量。

```bash
# 1. 启动 echo 服务器（0.0.0.0:8099，4 个 IO 线程）
./bin/examples/net/pingpong_server 8099 4

# 2. 另开终端启动压测客户端：4 连接 x 16KB 消息 x 10 秒
./bin/examples/net/pingpong_client 127.0.0.1 8099 4 16384 10
```

- 客户端每条连接独占一个 `EventLoop` 线程，严格 ping-pong：同一时刻每条连接最多一个消息在途。
- 压测期间每秒打印全局 round trips/s 与 MB/s，结束后输出汇总与每连接明细。
- 全局计数使用 atomic 无锁累加，统计定时器跑在主线程 loop，不干扰 IO 线程。

### 综合压测：QPS / P99 延迟 / 错误率 / 并发连接数

`examples/net/tcp_bench` 提供更完整的压测能力（服务器端仍用 `pingpong_server`）。标准测试流程：

```bash
# 0. 调大 fd 上限（10K 连接约需 20K fd）
ulimit -n 65535

# 1. 启动服务器：4 个 IO 线程，第 3 个参数为连接数周期打印间隔（conntest 核对 accept 数用）
./bin/examples/net/pingpong_server 8099 4 1

# 2. 纯 RTT / P99 延迟：窗口 1 严格 ping-pong（1 连接与 4 连接对比空闲唤醒惩罚）
./bin/examples/net/tcp_bench 127.0.0.1 8099 -c 1 -t 1 -s 1024 -d 5 -w 1
./bin/examples/net/tcp_bench 127.0.0.1 8099 -c 4 -t 4 -s 1024 -d 5 -w 1

# 3. QPS / 带宽：窗口 10 流水线，16 连接 x 16KB，吞吐由 CPU/带宽决定而非 RTT
./bin/examples/net/tcp_bench 127.0.0.1 8099 -c 16 -t 4 -s 16384 -d 5 -w 10

# 4. 交叉验证：pingpong_client 严格 ping-pong
./bin/examples/net/pingpong_client 127.0.0.1 8099 4 16384 5

# 5. 并发连接规模：建立 5000 条空闲连接并保持，输出成功数与每连接内存
./bin/examples/net/tcp_bench 127.0.0.1 8099 --conntest 5000 -t 4 --connect-timeout 45
```

#### 实测参考（VMware 4 核，2026-10-12，当日原始 socket RTT 基线 170µs）

| 场景 | QPS / 吞吐 | P50 | P99 | 错误率 |
|---|---|---|---|---|
| w=1 单连接 × 1KB | 4,903 | 188µs | 400µs | 0% |
| w=1 四连接 × 1KB | 109,256 | 29µs | 170µs | 0% |
| w=10 十六连接 × 16KB | 146,061（2.28 GB/s） | 826µs | 4.2ms | 0% |
| pingpong 4 连接 × 16KB | 104,464 trips/s | 38µs/RT | — | 0% |
| conntest 5000 连接 | 1.01s 全部建立 | — | — | 3.7 KB/连接 |

> 单连接 P50 显著高于四连接是虚拟机的空闲唤醒惩罚（每轮往返都走完整的空闲→唤醒路径），非框架瓶颈；评估框架延迟开销以多连接热态数据为准（29µs/RT 含两端完整框架路径）。VM 环境 RTT 地板日间波动大（实测 170~530µs），跨天/跨环境对比需注明当日基线。

实现要点：

- **QPS**：每连接 `window` 条消息在途（流水线），收到回包立即补发；回包批量到达时按消息长度切块统计，吞吐由 CPU/带宽决定而非 RTT。
- **P99 延迟**：发送时刻入 FIFO、回包到达按序弹出算 RTT（TCP 有序保证对应），1µs 粒度直方图，结束合并全部连接输出 P50/P90/P99/P99.9。
- **错误率**：消息前 8 字节携带每连接递增 seq，回包逐块校验 seq 与长度，错误率 = 校验失败块数 / 总回包数；另统计压测期间异常断开数。
- **并发连接数**：N 个 `TcpClient` 均布到 `threads` 个 loop（多连接共享 loop，避免一连接一线程），统计超时窗口内成功建立数与客户端 RSS 增量（近似每连接内存）。Connector 失败会无限重试，故失败数 = 超时内未建立数。

### HTTP 解析器微基准

`examples/net/http_bench` 对 `HttpRequestParser` / `HttpResponseParser`（Ragel 状态机）做纯 CPU 吞吐测试，模拟服务器真实路径（对齐 `HttpContext::parseRequest`）：每条消息 `reset()`（重建 `HttpRequest` 对象 + 重绑回调）+ `execute()` 解析头部 + `setBody()` 拷贝消息体（解析器只消费到 header_done，body 由调用方处理）：

```bash
./bin/examples/net/http_bench -n 100000
```

覆盖 GET 无 body / POST 1KB / 10 头部 / POST 64KB / 响应解析 5 个场景，输出 req/s、MB/s、ns/req 并校验解析正确性（错误计入 errors）。参考值：node http-parser 在裸机约 40-80 万 req/s/核，虚拟化环境会明显偏低。

## 线程模型与使用约定

- **一个 `EventLoop` 对象只能在其所属线程调用 `loop()`**；回调（connection/message/timer）都在该线程执行，业务代码无需加锁。
- 跨线程修改连接（如从业务线程发消息）：用 `loop->runInLoop([...]{ conn->send(...); })` 保证线程安全。
- 多线程监听：`TcpServer::Option::kReusePort` 可配合多进程/多线程 SO_REUSEPORT 负载均衡。
- `Buffer` 为 muduo 式「prependable + readable + writable」三段结构，支持高效粘包/拆包处理与协议头预留空间。

## 性能设计要点

- epoll 水平触发（LT），配合非阻塞 fd；`Poller::poll` 超时由最近到期定时器驱动。
- `Channel` 不拥有 fd，fd 生命周期由 `Socket` RAII 管理。
- 写缓冲：先尝试直接写，写不完才入 `outputBuffer` 并监听 WRITE 事件，避免小包频繁注册。
- `TcpConnection::setTcpNoDelay(bool)` 由调用方按需禁用 Nagle：小包立即发出，避免与延迟 ACK 叠加产生 40ms 级停顿（低延迟场景在 `connectionCallback` 中开启）。
- `TcpConnection` 状态为原子变量，`send`/`shutdown`/`forceClose` 可跨线程安全调用。
- `TcpServer` 用 Round-Robin（`EventLoopThreadPool::getNextLoop`）均摊连接，避免惊群。

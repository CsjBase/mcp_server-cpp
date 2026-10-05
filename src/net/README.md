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

auto addr = std::make_shared<net::Address>("0.0.0.0", 8080);
net::TcpServer server(&loop, addr, "echo");

server.setThreadNum(4);   // 4 个 IO 线程
server.setConnectionCallback([](const net::TcpConnection::ptr &conn) {
    if (conn->connected()) {
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
auto serverAddr = std::make_shared<net::Address>("127.0.0.1", 8080);
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
auto addr = std::make_shared<net::Address>("0.0.0.0", 8080);

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

## 线程模型与使用约定

- **一个 `EventLoop` 对象只能在其所属线程调用 `loop()`**；回调（connection/message/timer）都在该线程执行，业务代码无需加锁。
- 跨线程修改连接（如从业务线程发消息）：用 `loop->runInLoop([...]{ conn->send(...); })` 保证线程安全。
- 多线程监听：`TcpServer::Option::kReusePort` 可配合多进程/多线程 SO_REUSEPORT 负载均衡。
- `Buffer` 为 muduo 式「prependable + readable + writable」三段结构，支持高效粘包/拆包处理与协议头预留空间。

## 性能设计要点

- epoll 水平触发（LT），配合非阻塞 fd；`Poller::poll` 超时由最近到期定时器驱动。
- `Channel` 不拥有 fd，fd 生命周期由 `Socket` RAII 管理。
- 写缓冲：先尝试直接写，写不完才入 `outputBuffer` 并监听 WRITE 事件，避免小包频繁注册。
- `TcpServer` 用 Round-Robin（`EventLoopThreadPool::getNextLoop`）均摊连接，避免惊群。

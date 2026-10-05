# redis — Redis 客户端模块

基于 [hiredis](https://github.com/redis/hiredis) 的 Redis 客户端封装：同步（阻塞式）与异步（回调式，集成 [net](../net/README.md) EventLoop）两种客户端，通过统一基类 `IRedis` 承载连接生命周期与元信息，为连接池管理（借出/回收/淘汰只依赖 `isConnected/close/lastActiveTime`）预留统一接口。

## 目录结构

```
redis/
├── IRedis.h      # 连接统一基类：地址/密码/活跃时间等元信息
├── Redis.h / .cc # 同步客户端（redisContext 封装）
├── AsyncRedis.h / .cc # 异步客户端（redisAsyncContext + EventLoop/Channel）
└── Log.h         # 模块内日志别名
```

## 接口分层

```
IRedis                    连接元信息基类
├── isConnected() / close()   （供连接池统一管理）
├── ip / port / passwd / name
└── lastActiveTime            （池化淘汰判定用）

ISyncRedis  (IRedis)      同步命令接口
├── connect / reconnect / setTimeout
├── cmd(fmt, ...)         直接执行 → ReplyPtr
└── appendCmd + getReply  管道（pipelining）：批量入队，一次取回

IAsyncRedis (IRedis)      异步命令接口（回调式，EventLoop 线程内）
├── connect(ip, port, cb, timeoutMs)   非阻塞连接
├── cmd(argv / fmt, cb)                命令 + 回复回调
└── setErrorCallback(cb)               断连通知
```

`ReplyPtr` 为 `std::shared_ptr<redisReply>`，配合 `REDIS_NO_AUTO_FREE_REPLIES` 统一两种客户端的回复所有权语义。

## 快速上手：同步客户端

```cpp
#include "redis/Redis.h"

redis::Redis client;
client.connect("127.0.0.1", 6379, /*timeout_ms=*/3000);

// 单条命令
auto reply = client.cmd("SET %s %s", "key", "value");
if (reply && reply->type == REDIS_REPLY_STATUS) {
    // "OK"
}

auto val = client.cmd("GET %s", "key");
if (val && val->type == REDIS_REPLY_STRING) {
    std::string s(val->str, val->len);
}

// 管道：批量入队，减少 RTT
client.appendCmd("SET %s %s", "a", "1");
client.appendCmd("SET %s %s", "b", "2");
client.appendCmd("GET %s", "a");
auto r1 = client.getReply();
auto r2 = client.getReply();
auto r3 = client.getReply();

// 密码与超时
redis::Redis auth({{"ip", "127.0.0.1"}, {"port", "6379"},
                   {"passwd", "secret"}, {"timeout_ms", "500"}});
auth.connect();
```

## 快速上手：异步客户端

```cpp
#include "redis/AsyncRedis.h"

net::EventLoop loop;

auto client = std::make_shared<redis::AsyncRedis>(&loop);
client->setErrorCallback([](const std::string &err) {
    LOG_ERROR(LOGGER_DEFAULT(), "redis disconnected: {}", err);
});

client->connect("127.0.0.1", 6379, [](bool ok) {
    if (!ok) LOG_ERROR(LOGGER_DEFAULT(), "redis connect failed");
});

client->cmd("GET %s", [](redis::ReplyPtr reply) {
    // 出错时收到空 reply
    if (reply && reply->type == REDIS_REPLY_STRING)
        LOG_INFO(LOGGER_DEFAULT(), "got: {}", std::string(reply->str, reply->len));
}, "somekey");

loop.loop();
```

## 异步实现要点（hiredis 事件库适配器）

`AsyncRedis` 采用 hiredis 官方的「事件库适配器」路线：将 `redisAsyncContext` 的 `ev` 事件函数指针（`addRead/delRead/addWrite/delWrite/cleanup`）注入为 `net::Channel` 的 `enableReading/disableReading` 等实现 —— hiredis 负责连接握手、命令排队、回复解析与分发，`csj_net` 负责事件循环。

```
redisAsyncContext（hiredis）
   │  ev.addRead / delRead / addWrite / delWrite / cleanup
   ▼
net::Channel + EventLoop（epoll）
   │  handleRead / handleWrite / handleClose / handleError
   ▼
C 回调（onConnectCb / onDisconnectCb / replyCallback）→ C++ 状态机
```

关键语义：

- **线程模型**：所有操作与回调均在 EventLoop 线程执行（`assertInLoopThread`），跨线程请用 `loop->runInLoop/queueInLoop`；生命周期与 EventLoop 相同，须在 loop 线程析构。
- **连接**：`redisAsyncConnect` 非阻塞发起，可写事件到达时 hiredis 在内部完成握手并回调 `onConnect`（成功/失败）；失败上下文由本类在事件处理返回后补释放。
- **断连**：hiredis 检测连接死亡后自动释放上下文（`ev.cleanup` 幂等），未决命令的回调收到空 `ReplyPtr`，随后触发 `onDisconnect → errorCallback`。
- **回复所有权**：`REDIS_NO_AUTO_FREE_REPLIES` 下 reply 交由 `ReplyPtr` 管理；每条回复刷新 `lastActiveTime`（供池化淘汰）。

## 与连接池的配合

`IRedis` 将连接元信息（`isConnected` / `close` / `lastActiveTime` / 地址密码）与命令接口（`ISyncRedis` / `IAsyncRedis`）分离，池化实现可写成 `RedisPool<T>` 模板同时支持同步与异步连接，与 [db 模块 DBPool](../db/README.md) 的借还语义一致（借出独占、归还检查有效性、空闲淘汰）。

## 测试

单元测试位于 [tests/redis](../../tests/redis/)。

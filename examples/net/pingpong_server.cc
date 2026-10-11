/**
 * @file pingpong_server.cc
 * @brief net 模块 ping-pong 压测服务器：收到多少就原样回多少（echo）。
 *
 * 与 pingpong_client / tcp_bench 配合使用：客户端发固定大小消息，服务器回显，
 * 客户端据此统计每秒往返次数（round trips/s）、吞吐量、延迟分位与错误率。
 *
 * 用法: pingpong_server [port] [num_threads] [conn_log_interval]
 *   port              监听端口，默认 8099
 *   num_threads       IO 线程数（subLoop 数量），默认 4
 *   conn_log_interval 每隔多少秒打印一次当前连接数，0 表示不打印，默认 0
 *                     （tcp_bench --conntest 压连接规模时用于核对 accept 数）
 */
#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "net/base/Address.h"
#include "net/base/Log.h"

#include <atomic>
#include <cstdlib>
#include <memory>

namespace
{
    std::atomic<int64_t> g_connCount{0}; ///< 当前在线连接数（连接回调内维护）
}

int main(int argc, char *argv[])
{
    uint16_t port = argc > 1 ? static_cast<uint16_t>(atoi(argv[1])) : 8099;
    int threads = argc > 2 ? atoi(argv[2]) : 4;
    double connLogInterval = argc > 3 ? atof(argv[3]) : 0;

    net::EventLoop loop;
    auto addr = std::make_shared<net::InetAddress>("0.0.0.0", port);
    net::TcpServer server(&loop, addr, "pingpongServer");

    // 多 IO 线程：新连接由 mainLoop Round-Robin 均摊到各 subLoop
    server.setThreadNum(threads);

    server.setConnectionCallback([](const net::TcpConnection::ptr &conn)
                                 {
                                     if (conn->connected())
                                     {
                                         // echo 回包对延迟敏感，禁用 Nagle 避免小包停顿
                                         conn->setTcpNoDelay(true);
                                         g_connCount.fetch_add(1);
                                         NET_LOG_INFO("new conn [{}] from {}, online {}",
                                                      conn->name(), conn->peerAddress()->to_string(),
                                                      g_connCount.load());
                                     }
                                     else
                                     {
                                         g_connCount.fetch_sub(1);
                                         NET_LOG_INFO("conn [{}] closed, online {}",
                                                      conn->name(), g_connCount.load());
                                     }
                                 });

    // echo：把本次读到的数据原样发回。
    // 处于 loop 线程内时 send(Buffer*) 直接走 sendInLoop，不做中间拷贝。
    server.setMessageCallback([](const net::TcpConnection::ptr &conn, net::Buffer *buf, net::Timestamp)
                              {
                                  conn->send(buf);
                                  buf->retrieveAll();
                              });

    server.start();
    NET_LOG_INFO("pingpong server listening on 0.0.0.0:{} with {} io threads", port, threads);

    // 连接规模压测时周期性打印在线连接数，便于与客户端统计核对
    if (connLogInterval > 0)
    {
        loop.runEvery(connLogInterval, []
                      { NET_LOG_INFO("online connections: {}", g_connCount.load()); });
    }

    loop.loop();
    return 0;
}

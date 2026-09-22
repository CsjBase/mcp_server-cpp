#pragma once

#include "net/TcpServer.h"
#include "net/http/HttpContext.h"
#include "mcp/json_rpc/MethodDispatcher.h"
#include "StreamableHttpMessageWriter.h"
#include "utils/thread_pool.h"

namespace mcp
{

    class StreamableHttpServer
    {
    public:
        StreamableHttpServer(net::EventLoop *loop,
                             net::Address::ptr listenAddr,
                             MethodDispatcher *dispatcher,
                             std::shared_ptr<utils::ThreadPool> thread_pool);

        void start(int numThreads = 0);

    private:
        void onConnection(const net::TcpConnection::ptr &conn);
        void onMessage(const net::TcpConnection::ptr &conn,
                       net::Buffer *buf,
                       net::Timestamp receiveTime);

        void onRequest(const net::TcpConnection::ptr conn, const net::http::HttpRequest::ptr req);

        // MCP 2026-07-28 必需的头部校验
        bool validate_mcp_headers(const net::TcpConnection::ptr conn, const net::http::HttpRequest::ptr req);

        bool send_error(const net::TcpConnection::ptr conn, const net::http::HttpRequest::ptr req,
                        net::http::HttpStatus http_statu, int jsonrpc_code,
                        const std::string &msg, json request_id = nullptr);
        bool is_valid_origin(const std::string &) { return true; } // 生产环境需实现白名单

        net::TcpServer server_;
        MethodDispatcher *dispatcher_;
        std::shared_ptr<utils::ThreadPool> business_pool_; // 共享持有
    };

} // namespace mcp
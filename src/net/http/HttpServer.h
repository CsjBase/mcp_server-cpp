#pragma once

#include "net/TcpServer.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "Servlet.h"

namespace net
{
    namespace http
    {
        class HttpServer
        {
        public:
            HttpServer(bool keepalive,
                       EventLoop *loop,
                       Address::ptr listenAddr,
                       const std::string &name,
                       TcpServer::Option option = TcpServer::Option::kNoReusePort);

            void start(int numThreads = 0);

            /**
             * @brief 获取ServletDispatch
             */
            ServletDispatch::ptr getServletDispatch() const { return m_dispatch; }

            /**
             * @brief 设置ServletDispatch
             */
            void setServletDispatch(ServletDispatch::ptr v) { m_dispatch = v; }

        private:
            void onConnection(const TcpConnection::ptr &conn);
            void onMessage(const TcpConnection::ptr &conn,
                           Buffer *buf,
                           Timestamp receiveTime);
            void onRequest(const TcpConnection::ptr &, HttpRequest::ptr);

        private:
            bool m_keepalive;
            ServletDispatch::ptr m_dispatch;
            TcpServer m_server;
        };
    }
}
#include "HttpServer.h"
#include "HttpContext.h"

namespace net
{
    namespace http
    {
        HttpServer::HttpServer(bool keepalive,
                               EventLoop *loop,
                               Address::ptr listenAddr,
                               const std::string &name,
                               TcpServer::Option option)
            : m_keepalive(keepalive),
              m_dispatch(std::make_shared<ServletDispatch>()),
              m_server(loop, listenAddr, name, option)
        {
            m_server.setConnectionCallback(std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
            m_server.setMessageCallback(
                std::bind(&HttpServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        }

        void HttpServer::start(int numThreads)
        {
            if (numThreads > 0)
            {
                m_server.setThreadNum(numThreads);
            }
            m_server.start();
        }

        void HttpServer::onConnection(const TcpConnection::ptr &conn)
        {
            if (conn->connected())
            {
                conn->setContext(std::make_shared<HttpContext>());
            }
        }
        void HttpServer::onMessage(const TcpConnection::ptr &conn,
                                   Buffer *buf,
                                   Timestamp receiveTime)
        {
            HttpContext::ptr session = std::any_cast<HttpContext::ptr>(conn->getContext());
            if (!session->parseRequest(buf))
            {
                conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
                conn->shutdown();
            }
            if (session->gotAll())
            {
                onRequest(conn, session->getRequest());
                session->reset();
            }
        }

        void HttpServer::onRequest(const TcpConnection::ptr &conn, HttpRequest::ptr req)
        {
            HttpResponse::ptr rsp(new HttpResponse(req->getVersion(), req->isClose() || !m_keepalive));
            m_dispatch->handle(req, rsp);
            conn->send(rsp->toString());
            if (rsp->isClose())
            {
                conn->shutdown();
            }
        }
    }
}
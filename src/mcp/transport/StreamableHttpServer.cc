#include "StreamableHttpServer.h"
#include "mcp/base/Log.h"

namespace mcp
{
    StreamableHttpServer::StreamableHttpServer(net::EventLoop *loop,
                                               net::Address::ptr listenAddr,
                                               MethodDispatcher *dispatcher,
                                               IExecutor &executor)
        : server_(loop, listenAddr, "McpHttpServer"),
          dispatcher_(dispatcher),
          executor_(executor)
    {
        server_.setConnectionCallback([this](auto &&PH1)
                                      { onConnection(std::forward<decltype(PH1)>(PH1)); });
        server_.setMessageCallback([this](auto &&PH1, auto &&PH2, auto &&PH3)
                                   { onMessage(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2), std::forward<decltype(PH3)>(PH3)); });
        MCP_LOG_INFO("StreamableHttpServer initialized on {}", listenAddr->to_string());
    }

    void StreamableHttpServer::start(int numThreads)
    {
        server_.setThreadNum(numThreads);
        server_.start();
        MCP_LOG_INFO("StreamableHttpServer started");
    }

    void StreamableHttpServer::onConnection(const net::TcpConnection::ptr &conn)
    {
        if (conn->connected())
        {
            conn->setContext(std::make_shared<net::http::HttpContext>());
        }
    }

    void StreamableHttpServer::onMessage(const net::TcpConnection::ptr &conn,
                                         net::Buffer *buf,
                                         net::Timestamp receiveTime)
    {
        net::http::HttpContext::ptr context = std::any_cast<net::http::HttpContext::ptr>(conn->getContext());
        if (!context->parseRequest(buf))
        {
            conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
            conn->shutdown();
        }
        if (context->gotAll())
        {
            onRequest(conn, context->getRequest());
            context->reset();
        }
    }

    void StreamableHttpServer::onRequest(const net::TcpConnection::ptr conn, const net::http::HttpRequest::ptr req)
    {
        // 所有请求日志记录
        MCP_LOG_INFO("HTTP {} {} from {}",
                     net::http::HttpMethodToString(req->getMethod()), req->getPath(), req->getHeader("X-Forwarded-For"));

        // 1. The client MUST use HTTP POST to send JSON-RPC messages.
        if (req->getMethod() != net::http::HttpMethod::POST || req->getPath() != "/mcp")
        {
            net::http::HttpResponse resp;
            resp.setClose(true);
            resp.setStatus(net::http::HttpStatus::NOT_FOUND);
            conn->send(resp.toString());
            conn->shutdown();
            return;
        }

        // MCP 头部校验
        if (!validate_mcp_headers(conn, req))
        {
            return;
        }

        // 创建消息写入器（延迟决策状态机）
        auto writer = std::make_shared<StreamableHttpMessageWriter>(conn, req, keepalive_interval_);

        // 提交到业务线程池处理
        executor_.execute([this, req, writer]()
                          {
            // 委托给 MethodDispatcher 处理
            try
            {
                auto result = dispatcher_->dispatch(req->getBody(), writer);

                switch (result.kind)
                {
                case DispatchOutcome::Kind::Response:
                    writer->write_response(result.payload.value());
                    break;
                case DispatchOutcome::Kind::Notification:
                    writer->write_accepted();
                    break;
                case DispatchOutcome::Kind::StreamOpened:
                    // SSE 流已通过 write_notification 打开，ack 已发出。
                    // 不写入任何响应，保持连接打开。
                    break;
                }
            }
            catch (const McpException& e) {
                MCP_LOG_WARN("McpException in business pool: code={} msg={}",
                            static_cast<int>(e.code()), e.what());
                writer->write_response(ErrorResponse{
                    std::nullopt,
                    {static_cast<int>(e.code()), e.what(), e.data()}}
                                        .to_json());
            }
            catch (const std::exception& e) {
                MCP_LOG_ERROR("unhandled exception in business pool: what={}", e.what());
                writer->write_response(make_error(
                                        ErrorCode::InternalError,
                                        std::nullopt,
                                        "internal error")
                                        .to_json());
            } });
    }

    // MCP 2026-07-28 必需的头部校验
    bool StreamableHttpServer::validate_mcp_headers(const net::TcpConnection::ptr conn, const net::http::HttpRequest::ptr req)
    {
        // 2. The client MUST include an header listing both and as supported content
        //    types.Acceptapplication/jsontext/event-stream
        if (req->getHeader("Accept").find("application/json") == std::string::npos ||
            req->getHeader("Accept").find("text/event-stream") == std::string::npos)
        {
            if (req->getHeader("Accept").find("*/*") == std::string::npos)
            {
                return send_error(conn, req, net::http::HttpStatus::NOT_ACCEPTABLE, -32000,
                                  "Not Acceptable: Client must accept both application/json and text/event-stream");
            }
        }

        // 校验 MCP-Protocol-Version 头部必须存在
        std::string protocol_ver = req->getHeader("MCP-Protocol-Version");
        if (protocol_ver.empty())
        {
            return send_error(conn, req, net::http::HttpStatus::BAD_REQUEST, 0,
                              "Missing MCP-Protocol-Version header");
        }

        // 校验 Origin 头部，防止 DNS rebinding 攻击[reference:2]
        std::string origin = req->getHeader("Origin");
        if (!origin.empty() && !is_valid_origin(origin))
        {
            MCP_LOG_WARN("Invalid Origin header rejected: {}", origin);
            return send_error(conn, req, net::http::HttpStatus::FORBIDDEN, 0, "Forbidden");
        }

        // 解析 body 中的 JSON-RPC 消息
        json body;
        try
        {
            body = json::parse(req->getBody());
        }
        catch (...)
        {
            return send_error(conn, req, net::http::HttpStatus::BAD_REQUEST, 0, "Invalid JSON");
        }

        // 校验 MCP-Protocol-Version 头部与 body 中 _meta 的一致性
        std::string body_ver;
        if (body.contains("params") && body["params"].contains("_meta") &&
            body["params"]["_meta"].contains("io.modelcontextprotocol/protocolVersion") &&
            body["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].is_string())
        {
            body_ver = body["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].get<std::string>();
        }
        if (protocol_ver != body_ver)
        {
            return send_error(conn, req, net::http::HttpStatus::BAD_REQUEST, -32020,
                              "Protocol version mismatch", body.contains("id") ? body["id"] : nullptr);
        }

        // 校验 Mcp-Method 头部
        std::string mcp_method = req->getHeader("Mcp-Method");
        if (!body.contains("method") || !body["method"].is_string() ||
            mcp_method != body["method"].get<std::string>())
        {
            return send_error(conn, req, net::http::HttpStatus::BAD_REQUEST, -32020,
                              "Mcp-Method header does not match body", body.contains("id") ? body["id"] : nullptr);
        }

        // 校验 Mcp-Name 头部
        std::string mcp_name = req->getHeader("Mcp-Name");
        std::string params_name;
        if (body.contains("params") && body["params"].contains("name") && body["params"]["name"].is_string())
        {
            params_name = body["params"]["name"].get<std::string>();
        }
        if (mcp_name != params_name)
        {
            return send_error(conn, req, net::http::HttpStatus::BAD_REQUEST, -32020,
                              "Mcp-Name header does not match body", body.contains("id") ? body["id"] : nullptr);
        }

        return true; // 校验通过
    }
    bool StreamableHttpServer::send_error(const net::TcpConnection::ptr conn,
                                          const net::http::HttpRequest::ptr req,
                                          net::http::HttpStatus http_statu,
                                          int jsonrpc_code,
                                          const std::string &msg, json request_id)
    {
        net::http::HttpResponse resp;
        resp.setStatus(http_statu);
        if (jsonrpc_code != 0)
        {
            resp.setHeader("Content-Type", "application/json");
            json error = {{"jsonrpc", "2.0"},
                          {"error", {{"code", jsonrpc_code}, {"message", msg}}}};
            if (!request_id.is_null())
            {
                error["id"] = request_id;
            }
            resp.setBody(error.dump());
        }
        else
        {
            if (!msg.empty())
                resp.setBody(msg);
        }
        resp.setClose(true);
        conn->send(resp.toString());
        conn->shutdown();
        return false;
    }
}
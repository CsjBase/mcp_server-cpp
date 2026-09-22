#pragma once

#include "mcp/json_rpc/JsonRpcContext.h"
#include "net/TcpConnection.h"
#include "net/http/HttpRequest.h"
#include "net/http/HttpResponse.h"

#include <assert.h>

namespace mcp
{

    class StreamableHttpMessageWriter : public IMessageWriter,
                                        public std::enable_shared_from_this<StreamableHttpMessageWriter>
    {
    public:
        enum class State
        {
            Direct,    // 初始状态，等待决策
            Streaming, // SSE流模式已激活
            Closed     // 流已关闭
        };

        StreamableHttpMessageWriter(const net::TcpConnection::ptr &conn,
                                    const net::http::HttpRequest::ptr req,
                                    double keepalive_interval = 30.0)
            : conn_(conn), request_(req),
              state_(State::Direct),
              keepalive_interval_(keepalive_interval)
        {
            assert(conn != nullptr);
        }
        virtual ~StreamableHttpMessageWriter();
        // 核心：根据当前状态决定写入行为
        void write_notification(const json &notification) override;

        void write_response(const json &response) override;

        void write_accepted();

        bool is_streaming() const override { return state_ == State::Streaming; }

    private:
        void upgrade_to_sse();
        void start_keepalive();
        void cancel_keepalive();

    private:
        net::TcpConnection::ptr conn_;
        net::http::HttpRequest::ptr request_;
        State state_;

        double keepalive_interval_;
        net::TimerId keepalive_timer_id_;
    };

} // namespace
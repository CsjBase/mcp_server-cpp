#include "StreamableHttpMessageWriter.h"
#include "mcp/base/Log.h"

namespace mcp
{

    StreamableHttpMessageWriter::~StreamableHttpMessageWriter()
    {
        cancel_keepalive();

        if (subscription_id_.has_value() && subscription_cleanup_)
        {
            try
            {
                subscription_cleanup_(subscription_id_.value());
            }
            catch (const std::exception &e)
            {
                MCP_LOG_ERROR("subscription cleanup failed: id={} what={}",
                              *subscription_id_, e.what());
            }
        }
    }
    void StreamableHttpMessageWriter::write_notification(const json &notification)
    {
        if (state_ == State::Closed)
        {
            return;
        }

        if (state_ == State::Direct)
        {
            // 收到第一个通知：升级为SSE流模式
            upgrade_to_sse();
        }

        // 按SSE格式写入：data: <json>\n\n
        std::string sse_frame = "data: " + notification.dump() + "\n\n";
        conn_->send(sse_frame);
    }

    void StreamableHttpMessageWriter::write_response(const json &response)
    {
        if (state_ == State::Closed)
            return;

        if (state_ == State::Direct)
        {
            // 没有通知，直接以application/json响应
            net::http::HttpResponse resp;
            resp.setStatus(net::http::HttpStatus::OK);
            resp.setHeader("Content-Type", "application/json");
            resp.setBody(response.dump());
            resp.setClose(true);
            conn_->send(resp.toString());
            conn_->shutdown();
        }
        else
        {
            // 已在SSE模式，写入最终响应并关闭流
            cancel_keepalive();
            std::string sse_frame = "data: " + response.dump() + "\n\n";
            conn_->send(sse_frame);
            conn_->shutdown();
        }
        state_ = State::Closed;
    }

    void StreamableHttpMessageWriter::write_accepted()
    {
        if (state_ == State::Direct)
        {
            // 未升级 → 直接发 202，这是规范要求的正确行为
            std::string resp =
                "HTTP/1.1 202 Accepted\r\n"
                "Content-Length: 0\r\n"
                "Connection: close\r\n"
                "\r\n";
            conn_->send(resp);
            conn_->shutdown();
        }
        else
        {
            // 已经升级到 SSE → 202 语义不适用，关闭流
            conn_->shutdown();
        }
        state_ = State::Closed;
    }

    void StreamableHttpMessageWriter::upgrade_to_sse()
    {
        // 发送SSE响应头
        std::string sse_header =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/event-stream\r\n"
            "Cache-Control: no-cache\r\n"
            "Connection: keep-alive\r\n"
            "\r\n";
        conn_->send(sse_header);
        state_ = State::Streaming;
        start_keepalive();
    }

    void StreamableHttpMessageWriter::start_keepalive()
    {
        if (keepalive_interval_ <= 0.0)
            return;

        if (keepalive_timer_id_.valid())
        {
            cancel_keepalive();
        }

        std::weak_ptr<StreamableHttpMessageWriter> weak_self = shared_from_this();

        keepalive_timer_id_ = conn_->getLoop()->runEvery(
            keepalive_interval_,
            [weak_self]()
            {
                auto self = weak_self.lock();
                if (!self)
                    return;

                if (self->state_ == State::Streaming &&
                    self->conn_ && self->conn_->connected())
                {
                    self->conn_->send(": keepalive\n\n");
                }
                else
                {
                    self->cancel_keepalive();
                }
            });
    }
    void StreamableHttpMessageWriter::cancel_keepalive()
    {
        if (keepalive_timer_id_.valid())
        {
            conn_->getLoop()->cancel(keepalive_timer_id_);
            keepalive_timer_id_ = net::TimerId();
        }
    }
}
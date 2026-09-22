#pragma once

#include "mcp/json_rpc/JsonRpcContext.h"
#include "mcp/transport/StdioTransport.h"

namespace mcp
{

    // 适配器：将 IMessageWriter 接口桥接到 StdioTransport
    //
    // 注意：本文件是 transport/ 目录下唯一依赖 jsonrpc 的文件。
    // 它不参与 StdioTransport 的依赖闭环，仅作为跨层桥接。
    class StdioMessageWriter : public IMessageWriter
    {
    public:
        explicit StdioMessageWriter(StdioTransport &transport)
            : transport_(transport) {}

        void write_notification(const json &notification) override
        {
            transport_.send(notification.dump());
        }

        void write_response(const json &response) override
        {
            transport_.send(response.dump());
        }

        // stdio 没有 Streamable HTTP 那样的响应形态延迟决策，
        // 通知总是可以立即写入 stdout
        bool is_streaming() const override { return true; }

    private:
        StdioTransport &transport_;
    };

} // namespace mcp
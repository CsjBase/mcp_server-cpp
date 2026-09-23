// src/main.cpp
#include "mcp/transport/StreamableHttpServer.h"
#include "mcp/json_rpc/MethodDispatcher.h"
#include "mcp/server/McpMethodHandlers.h"

int main()
{
    net::EventLoop loop;
    auto listenAddr = std::make_shared<net::InetAddress>(8080);

    // 构建 JSON-RPC 分发器
    mcp::MethodDispatcher dispatcher;
    mcp::McpMethodHandlers handlers(dispatcher);

    // 注册工具
    handlers.register_tool(mcp::ToolDescriptor::make(
        "echo", "Echo input",
        nlohmann::json{{"type", "object"},
                       {"properties", {{"text", {{"type", "string"}}}}},
                       {"required", {"text"}}},
        [](const nlohmann::json &args, mcp::IRequestContext &ctx)
        {
            ctx.report_progress(0.0, 1.0, "Starting...");
            // ... 业务逻辑 ...
            ctx.report_progress(1.0, 1.0, "Done");
            mcp::ToolResult result;
            result.content = nlohmann::json{{{"type", "text"}, {"text", args["text"]}}};
            return result;
        }));

    // 启动 HTTP 服务器
    auto business_pool = std::make_shared<utils::ThreadPool>(4, 4096);
    mcp::StreamableHttpServer server(&loop, listenAddr, &dispatcher, business_pool);
    server.start(4);

    loop.loop();
    return 0;
}
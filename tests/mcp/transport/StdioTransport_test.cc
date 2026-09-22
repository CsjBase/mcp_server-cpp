#include "mcp/json_rpc/MethodDispatcher.h"
#include "mcp/json_rpc/DefaultRequestContext.h"
#include "mcp/transport/StdioTransport.h"
#include "mcp/McpMethodHandlers.h"

int main()
{
    // ---- 构建 JSON-RPC 分发器 ----
    mcp::MethodDispatcher dispatcher;
    mcp::McpMethodHandlers handlers(dispatcher);

    // 注册工具（示例：echo）
    handlers.register_tool(mcp::ToolDescriptor::make(
        "echo",
        "Echo the input text",
        nlohmann::json{
            {"type", "object"},
            {"properties", {{"text", {{"type", "string"}}}}},
            {"required", {"text"}}},
        [](const nlohmann::json &args,
           mcp::IRequestContext &) -> nlohmann::json
        {
            return nlohmann::json{{"type", "text"}, {"text", args["text"]}};
        }));

    // ---- 构建 stdio 传输 ----
    mcp::StdioTransport transport;

    // 将 dispatcher 包装为 ITransport 期望的 MessageHandler 签名
    // dispatch 的 writer 参数在 stdio 场景下为 nullptr：
    // stdio 没有“响应模式”的概念，通知直接写入 stdout
    transport.set_handler(
        [&dispatcher](const std::string &raw)
            -> std::optional<std::string>
        {
            auto result = dispatcher.dispatch(raw, /*writer=*/nullptr);
            // MethodDispatcher 返回 optional<json>：
            //   有值 → 请求，序列化为字符串返回
            //   无值 → 通知，返回 nullopt
            if (result)
            {
                return result->dump();
            }
            return std::nullopt;
        });

    // ---- 阻塞运行 ----
    transport.start();
    return 0;
}
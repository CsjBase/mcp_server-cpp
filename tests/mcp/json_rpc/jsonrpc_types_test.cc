#include "mcp/json_rpc/JsonRpcTypes.h"
#include "mcp/json_rpc/MethodDispatcher.h"

#include <iostream>

using namespace mcp;

int main()
{
    // ---- 1. 构建带 _meta 的 MCP 请求 ----
    RequestMeta meta;
    meta.client_info = json{{"name", "my-client"}, {"version", "1.0.0"}};
    meta.client_capabilities = json{{"tools", true}};

    Request req;
    req.id = RequestId(int64_t{1});
    req.method = "tools/call";
    req.params = build_mcp_params(
        json{{"name", "search"}, {"arguments", {{"query", "MCP"}}}},
        meta);

    json wire = req.to_json();
    std::cout << "Request wire:\n"
              << wire.dump(2) << "\n\n";

    // ---- 2. 构建成功响应（自动注入 resultType） ----
    auto success = SuccessResponse::make(
        RequestId(int64_t{1}),
        json{{"content", {{{"type", "text"}, {"text", "ok"}}}}});
    std::cout << "Success wire:\n"
              << success.to_json().dump(2) << "\n\n";

    // ---- 3. 构建错误响应 ----
    auto err = make_error(ErrorCode::InvalidParams, RequestId(int64_t{1}),
                          "missing required parameter 'name'");
    std::cout << "Error wire:\n"
              << err.to_json().dump(2) << "\n\n";

    std::cout << "=============================================================\n";
    // ---- 4. 解析未知消息 ----
    json incoming1 = json::parse(R"({"jsonrpc":"2.0","id":1001,"method":"tools/call","params":{"name":"get_weather","arguments":{"city":"Beijing"},"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"my-client","version":"0.1.0"},"io.modelcontextprotocol/clientCapabilities":{}}}})");
    json incoming2 = json::parse(R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28"}}})");
    json incoming3 = json::parse(R"({"jsonrpc":"2.0","id":1001,"result":{"content":[{"type":"text","text":"Beijing, 26°C sunny"}],"isError":false,"_meta":{"io.modelcontextprotocol/serverInfo":{"name":"weather-server","version":"1.0.0"}}}})");
    json incoming4 = json::parse(R"({"jsonrpc":"2.0","id":1001,"error":{"code":-32602,"message":"Invalid params","data":{"reason":"missing required argument: city"}}})");
    json incoming5 = json::parse(R"({"jsonrpc":"2.0","id":1001,"result":{"content":[{"type":"text","text":"Rate limit exceeded"}],"isError":true,"_meta":{"io.modelcontextprotocol/serverInfo":{"name":"weather-server","version":"1.0.0"}}}})");
    auto msg1 = JsonRpcMessage::from_json(incoming1);
    auto msg2 = JsonRpcMessage::from_json(incoming2);
    auto msg3 = JsonRpcMessage::from_json(incoming3);
    auto msg4 = JsonRpcMessage::from_json(incoming4);
    auto msg5 = JsonRpcMessage::from_json(incoming5);
    if (msg1.has_value())
    {
        std::visit([&](const auto &arg)
                   {
            if(arg.to_json()==incoming1){
                std::cout << "incoming1 Parsed successfully\n";
            } }, msg1.value().value);
    }
    if (msg2)
    {
        std::visit([&](const auto &arg)
                   {
            if(arg.to_json()==incoming2){
                std::cout << "incoming2 Parsed successfully\n";
            } }, msg2.value().value);
    }
    if (msg3)
    {
        std::visit([&](const auto &arg)
                   {
            if(arg.to_json()==incoming3){
                std::cout << "incoming3 Parsed successfully\n";
            } }, msg3.value().value);
    }
    if (msg4)
    {
        std::visit([&](const auto &arg)
                   {
            if(arg.to_json()==incoming4){
                std::cout << "incoming4 Parsed successfully\n";
            } }, msg4.value().value);
    }
    if (msg5)
    {
        std::visit([&](const auto &arg)
                   {
            if(arg.to_json()==incoming5){
                std::cout << "incoming5 Parsed successfully\n";
            } }, msg5.value().value);
    }

    std::cout << std::endl;

    MethodDispatcher m;
    m.dispatch("{bad json", nullptr);

    return 0;
}
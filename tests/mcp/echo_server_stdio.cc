#include "mcp/McpServer.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

int main()
{
    mcp::McpServer server({.name = "echo-stdio",
                           .version = "1.0.0"});

    server.register_tool(mcp::ToolDescriptor::make(
        "echo", "Echo the input text",
        json{
            {"type", "object"},
            {"properties", {{"text", {{"type", "string"}}}}},
            {"required", {"text"}}},
        [](const json &args, mcp::IRequestContext &)
        {
            mcp::ToolResult r;
            r.content = json::array({{{"type", "text"}, {"text", args["text"]}}});
            r.structured_content = json{
                {"echoed", args["text"]},
                {"length", args["text"].get<std::string>().size()}};
            return r;
        }));

    // {"jsonrpc":"2.0","id":"server-discover-probe-1","method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"mcp-inspector","version":"0.0.0"},"io.modelcontextprotocol/clientCapabilities":{"sampling":{},"elicitation":{"form":{},"url":{}},"roots":{"listChanged":true},"tasks":{"list":{},"cancel":{},"requests":{"sampling":{"createMessage":{}},"elicitation":{"create":{}}}},"extensions":{"io.modelcontextprotocol/tasks":{},"io.modelcontextprotocol/ui":{"mimeTypes":["text/html;profile=mcp-app"],"elicitation":{}},"io.modelcontextprotocol/skills":{}}}}}}
    // {"method":"tools/list","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"mcp-inspector","version":"0.0.0"},"io.modelcontextprotocol/clientCapabilities":{"sampling":{},"elicitation":{"form":{},"url":{}},"roots":{"listChanged":true},"tasks":{"list":{},"cancel":{},"requests":{"sampling":{"createMessage":{}},"elicitation":{"create":{}}}},"extensions":{"io.modelcontextprotocol/tasks":{},"io.modelcontextprotocol/ui":{"mimeTypes":["text/html;profile=mcp-app"],"elicitation":{}},"io.modelcontextprotocol/skills":{}}},"io.modelcontextprotocol/logLevel":"debug","progressToken":1}},"jsonrpc":"2.0","id":1}
    // {"method":"tools/call","params":{"name":"echo","arguments":{"text":"hello world"},"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientInfo":{"name":"mcp-inspector","version":"0.0.0"},"io.modelcontextprotocol/clientCapabilities":{"sampling":{},"elicitation":{"form":{},"url":{}},"roots":{"listChanged":true},"tasks":{"list":{},"cancel":{},"requests":{"sampling":{"createMessage":{}},"elicitation":{"create":{}}}},"extensions":{"io.modelcontextprotocol/tasks":{},"io.modelcontextprotocol/ui":{"mimeTypes":["text/html;profile=mcp-app"],"elicitation":{}},"io.modelcontextprotocol/skills":{}}},"io.modelcontextprotocol/logLevel":"debug","progressToken":6}},"jsonrpc":"2.0","id":6}

    server.run_stdio();
    return 0;
}
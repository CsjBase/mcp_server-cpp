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

    server.run_stdio();
    return 0;
}
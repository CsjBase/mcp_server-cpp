#include "mcp/McpServer.h"

#include <nlohmann/json.hpp>
#include <thread>

using json = nlohmann::json;

int main()
{
    mcp::McpServer server({.name = "full-http",
                           .version = "1.0.0",
                           .io_threads = 4,
                           .business_threads = 8,
                           .sse_keepalive_interval = 30.0});

    // 工具
    server.register_tool(mcp::ToolDescriptor::make(
        "slow_task", "A long-running task that reports progress",
        json{
            {"type", "object"},
            {"properties", {{"steps", {{"type", "integer"}, {"minimum", 1}, {"maximum", 100}}}}},
            {"required", {"steps"}}},
        [](const json &args, mcp::IRequestContext &ctx)
        {
            int steps = args["steps"];
            for (int i = 1; i <= steps; ++i)
            {
                ctx.report_progress(
                    static_cast<double>(i) / steps, 1.0,
                    "Step " + std::to_string(i) + "/" +
                        std::to_string(steps));
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100));
            }
            mcp::ToolResult r;
            r.content = json::array({{{"type", "text"}, {"text", "Completed " + std::to_string(steps) + " steps"}}});
            return r;
        }));

    // 资源
    server.register_resource(mcp::ResourceDescriptor{
        .uri = "file:///config/server.json",
        .name = "Server Configuration",
        .description = "Runtime configuration",
        .mime_type = "application/json",
        .reader = []() -> json
        {
            return json::array({{{"uri", "file:///config/server.json"},
                                 {"mimeType", "application/json"},
                                 {"text", R"({"port":8080,"logLevel":"info"})"}}});
        }});

    // 资源模板
    server.register_resource_template(
        mcp::ResourceTemplateDescriptor{
            .uri_template = "file:///logs/{date}.log",
            .name = "Daily Log",
            .description = "Log file for a given date",
            .mime_type = "text/plain",
            .reader = [](const std::string &uri,
                         const std::unordered_map<std::string,
                                                  std::string> &params) -> json
            {
                return json::array({{{"uri", uri},
                                     {"mimeType", "text/plain"},
                                     {"text", "Log content for " + params.at("date")}}});
            }});

    // Prompt
    server.register_prompt(mcp::PromptDescriptor{
        .name = "code_review",
        .description = "Review a code snippet",
        .arguments = {
            {"language", "Programming language", true},
            {"code", "Code to review", true}},
        .renderer = [](const json &args) -> json
        {
            return json::array({{{"role", "system"},
                                 {"content", {{"type", "text"}, {"text", "You are an expert " + args["language"].get<std::string>() + " code reviewer."}}}},
                                {{"role", "user"},
                                 {"content", {{"type", "text"}, {"text", "Review:\n" + args["code"].get<std::string>()}}}}});
        }});

    server.listen_http("0.0.0.0", 8080);
    return 0;
}
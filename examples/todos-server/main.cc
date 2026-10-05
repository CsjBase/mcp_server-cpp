// examples/todos/main.cpp
#include "mcp/McpServer.h"
#include "TodoStore.h"
#include "mcp/base/Log.h"

#include <cstring>
#include <string>

// 前向声明
namespace todos
{
    void register_tools(mcp::McpServer &,
                        std::shared_ptr<TodoStore>);
    void register_resources(mcp::McpServer &,
                            std::shared_ptr<TodoStore>);
    void register_prompts(mcp::McpServer &,
                          std::shared_ptr<TodoStore>);
}

// --http --port 8088 --bind 0.0.0.0
int main(int argc, char **argv)
{
    bool use_stdio = true;
    std::string http_addr = "0.0.0.0";
    uint16_t http_port = 8080;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--http") == 0)
        {
            use_stdio = false;
        }
        else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            http_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (std::strcmp(argv[i], "--bind") == 0 && i + 1 < argc)
        {
            http_addr = argv[++i];
        }
    }

    // 应用级存储。与协议层无关。
    auto store = std::make_shared<todos::TodoStore>();
    store->init_schema();

    // 组装服务端
    mcp::McpServer server({.name = "todos-server",
                           .version = "1.0.0",
                           .io_threads = 4,
                           .business_threads = 4,
                           .business_queue_limit = 1024,
                           .sse_keepalive_interval = 30.0,
                           .supported_versions = {"2026-07-28"},
                           .discover_ttl_ms = 3600000,
                           .instructions =
                               "This server manages a personal todo list. "
                               "Use create_todo to add items, list_todos to see them, "
                               "and complete_todo or delete_todo with the returned "
                               "todo_id to modify them. "
                               "The todos://all resource provides the current list, "
                               "and the daily_review prompt helps plan the day."});

    todos::register_tools(server, store);
    todos::register_resources(server, store);
    todos::register_prompts(server, store);

    if (use_stdio)
    {
        MCP_LOG_INFO("Starting todos-server on stdio");
        server.run_stdio();
    }
    else
    {
        MCP_LOG_INFO("Starting todos-server on {}:{}",
                     http_addr, http_port);
        server.listen_http(http_addr, http_port);
    }
    return 0;
}
#include "mcp/McpServer.h"
#include "mcp/base/Log.h"
#include "TodoStore.h"

namespace todos
{

    using json = nlohmann::json;
    using mcp::IRequestContext;
    using mcp::ToolDescriptor;
    using mcp::ToolResult;

    void register_tools(mcp::McpServer &server,
                        std::shared_ptr<TodoStore> store)
    {
        // ============================================================
        // create_todo
        // ============================================================
        server.register_tool(ToolDescriptor::make(
            "create_todo",
            "Create a new todo item. Returns a todo_id that can be "
            "used with complete_todo, delete_todo, and get_todo.",
            json{
                {"type", "object"},
                {"properties", {{"title", {{"type", "string"}, {"minLength", 1}, {"maxLength", 200}, {"description", "The todo title"}}}}},
                {"required", {"title"}}},
            [store](const json &args, IRequestContext &) -> ToolResult
            {
                try
                {
                    auto todo = store->create(args["title"]);

                    ToolResult r;
                    r.content = json::array({{{"type", "text"},
                                              {"text", "Created " + std::to_string(todo.id) + ": " + todo.title}}});
                    r.structured_content = json{{"todo", todo.to_json()}};
                    return r;
                }
                catch (const std::exception &e)
                {
                    // 数据库错误 → 工具执行错误，不是协议错误。
                    // 返回 isError，让模型看到"存储层失败"并可能重试。
                    ToolResult r;
                    r.content = json::array({{{"type", "text"},
                                              {"text", std::string("Failed to create todo: ") +
                                                           e.what()}}});
                    r.is_error = true;
                    return r;
                }
            },
            json{
                {"type", "object"},
                {"properties", {{"todo", {{"type", "object"}, {"properties", {{"id", {{"type", "integer"}}}, {"title", {{"type", "string"}}}, {"completed", {{"type", "boolean"}}}, {"createdAt", {{"type", "integer"}}}}}, {"required", {"id", "title", "completed", "createdAt"}}}}}},
                {"required", {"todo"}}}));

        // ============================================================
        // list_todos
        // ============================================================
        server.register_tool(ToolDescriptor::make(
            "list_todos",
            "List all todo items, optionally filtered by completion status.",
            json{
                {"type", "object"},
                {"properties", {{"completed", {{"type", "boolean"}, {"description", "If set, only return todos with this completion status"}}}}}},
            [store](const json &args, IRequestContext &) -> ToolResult
            {
                try
                {
                    std::vector<Todo> todos;
                    if (args.contains("completed"))
                    {
                        todos = store->list_by_status(
                            args["completed"].get<bool>());
                    }
                    else
                    {
                        todos = store->list();
                    }

                    ToolResult r;

                    if (todos.empty())
                    {
                        r.content = json::array({{{"type", "text"}, {"text", "No todos found."}}});
                        r.structured_content = json::array();
                    }
                    else
                    {
                        json items = json::array();
                        std::string text = std::to_string(todos.size()) +
                                           " todo(s):\n";
                        for (const auto &t : todos)
                        {
                            items.push_back(t.to_json());
                            text += t.completed ? "  [completed] "
                                                : "  [incomplete] ";
                            text += std::to_string(t.id) + " " +
                                    t.title + "\n";
                        }
                        r.content = json::array({{{"type", "text"}, {"text", text}}});
                        r.structured_content = json{{"todos", items}};
                    }

                    return r;
                }
                catch (const std::exception &e)
                {
                    ToolResult r;
                    r.content = json::array({{{"type", "text"},
                                              {"text", std::string("Failed to list todos: ") +
                                                           e.what()}}});
                    r.is_error = true;
                    return r;
                }
            },
            json{
                {"type", "object"},
                {"properties", {{"todos", {{"type", "array"}, {"items", {{"type", "object"}, {"properties", {{"id", {{"type", "integer"}}}, {"title", {{"type", "string"}}}, {"completed", {{"type", "boolean"}}}, {"createdAt", {{"type", "integer"}}}}}, {"required", {"id", "title", "completed", "createdAt"}}}}}}}},
                {"required", {"todos"}}}));

        // ============================================================
        // complete_todo
        // ============================================================
        server.register_tool(ToolDescriptor::make(
            "complete_todo",
            "Mark a todo item as completed.",
            json{
                {"type", "object"},
                {"properties", {{"id", {{"type", "integer"}, {"minLength", 1}, {"description", "The todo_id returned by create_todo"}}}}},
                {"required", {"id"}}},
            [store](const json &args, IRequestContext &) -> ToolResult
            {
                int64_t id = args["id"].get<int64_t>();
                try
                {
                    if (!store->complete(id))
                    {
                        // id 不存在 → 语义错误，返回 isError
                        ToolResult r;
                        r.content = json::array({{{"type", "text"},
                                                  {"text", "Todo not found: " + id}}});
                        r.is_error = true;
                        return r;
                    }

                    auto todo = store->get(id);
                    ToolResult r;
                    r.content = json::array({{{"type", "text"},
                                              {"text", "Completed " + std::to_string(id) + ": " + todo->title}}});
                    r.structured_content = json{{"todo", todo->to_json()}};
                    return r;
                }
                catch (const std::exception &e)
                {
                    ToolResult r;
                    r.content = json::array({{{"type", "text"},
                                              {"text", std::string("Failed to complete todo: ") +
                                                           e.what()}}});
                    r.is_error = true;
                    return r;
                }
            }));

        // ============================================================
        // delete_todo
        // ============================================================
        server.register_tool(ToolDescriptor::make(
            "delete_todo",
            "Delete a todo item permanently.",
            json{
                {"type", "object"},
                {"properties", {{"id", {{"type", "integer"}, {"minLength", 1}}}}},
                {"required", {"id"}}},
            [store](const json &args, IRequestContext &) -> ToolResult
            {
                int64_t id = args["id"].get<int64_t>();
                try
                {
                    if (!store->remove(id))
                    {
                        ToolResult r;
                        r.content = json::array({{{"type", "text"},
                                                  {"text", "Todo not found: " + id}}});
                        r.is_error = true;
                        return r;
                    }

                    ToolResult r;
                    r.content = json::array({{{"type", "text"}, {"text", "Deleted " + id}}});
                    r.structured_content = json{{"deletedId", id}};
                    return r;
                }
                catch (const std::exception &e)
                {
                    ToolResult r;
                    r.content = json::array({{{"type", "text"},
                                              {"text", std::string("Failed to delete todo: ") +
                                                           e.what()}}});
                    r.is_error = true;
                    return r;
                }
            }));

        MCP_LOG_INFO("Registered 4 SQL-backed todo tools");
    }

} // namespace todos
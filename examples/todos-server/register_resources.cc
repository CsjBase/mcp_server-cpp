#include "mcp/McpServer.h"
#include "TodoStore.h"

#include <charconv>

namespace todos
{

    using json = nlohmann::json;
    using mcp::ResourceDescriptor;
    using mcp::ResourceTemplateDescriptor;

    void register_resources(mcp::McpServer &server,
                            std::shared_ptr<TodoStore> store)
    {
        server.register_resource(ResourceDescriptor{
            .uri = "todos://all",
            .name = "All Todos",
            .description = "Complete list of todo items",
            .mime_type = "application/json",
            .reader = [store]() -> json
            {
                json items = json::array();
                for (const auto &t : store->list())
                {
                    items.push_back(t.to_json());
                }
                return json::array({{{"uri", "todos://all"},
                                     {"mimeType", "application/json"},
                                     {"text", items.dump(2)}}});
            }});

        server.register_resource_template(ResourceTemplateDescriptor{
            .uri_template = "todos://item/{id}",
            .name = "Todo Item",
            .description = "A single todo item by its ID",
            .mime_type = "application/json",
            .reader = [store](const std::string &uri,
                              const std::unordered_map<std::string,
                                                       std::string> &params)
                -> json
            {
                const std::string &id_str = params.at("id");
                int64_t id;

                if (std::from_chars(id_str.data(), id_str.data() + id_str.size(), id).ec != std::errc{})
                {
                    throw mcp::McpException(
                        mcp::ErrorCode::InvalidParams,
                        "Todo not found: " + id_str);
                }

                auto todo = store->get(id);
                if (!todo)
                {
                    throw mcp::McpException(
                        mcp::ErrorCode::InvalidParams,
                        "Todo not found: " + id_str);
                }
                return json::array({{{"uri", uri},
                                     {"mimeType", "application/json"},
                                     {"text", todo->to_json().dump(2)}}});
            }});
    }

} // namespace todos
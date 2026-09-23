#pragma once

#include "mcp/json_rpc/MethodDispatcher.h"

#include "ToolDescriptor.h"
#include "ResourceDescriptor.h"
#include "PromptDescriptor.h"
#include "UriTemplate.h"

namespace mcp
{

    class McpMethodHandlers
    {
    public:
        explicit McpMethodHandlers(MethodDispatcher &dispatcher);

        void register_tool(ToolDescriptor t);
        void register_resource(ResourceDescriptor r);
        void register_resource_template(ResourceTemplateDescriptor r);
        void register_prompt(PromptDescriptor p);

        // 冻结注册表。之后任何 register_* 调用都会抛异常。
        // 由 McpServer 在启动传输前调用。
        void freeze() { frozen_ = true; }

        void set_discover_config(std::string server_name,
                                 std::string server_version,
                                 std::vector<std::string> versions,
                                 std::optional<std::string> instructions,
                                 int64_t ttl_ms);

    private:
        void check_not_frozen(const char *op) const
        {
            if (frozen_)
            {
                throw std::logic_error(
                    std::string(op) + " called after freeze()");
            }
        }

        SuccessResponse handle_discover(const Request &req);

        SuccessResponse handle_tools_list(const Request &req);
        SuccessResponse handle_tools_call(
            const Request &req, IRequestContext &ctx);

        SuccessResponse handle_resources_list(const Request &req);
        SuccessResponse handle_resource_templates_list(
            const Request &req);
        SuccessResponse handle_resources_read(const Request &req);

        SuccessResponse handle_prompts_list(const Request &req);
        SuccessResponse handle_prompts_get(const Request &req);

    private:
        std::unordered_map<std::string, ToolDescriptor> tools_;
        std::unordered_map<std::string, ResourceDescriptor> resources_;
        struct CompiledTemplate
        {
            UriTemplate compiled;
            ResourceTemplateDescriptor descriptor;
        };

        std::unordered_map<std::string, CompiledTemplate> resource_templates_;
        std::unordered_map<std::string, PromptDescriptor> prompts_;

        bool frozen_ = false;

        // 由 McpServer 在构造时注入
        std::string server_name_ = "mcp-server";
        std::string server_version_ = "1.0.0";
        std::vector<std::string> supported_versions_;
        std::optional<std::string> instructions_;
        int64_t discover_ttl_ms_ = 3600000;
    };

} // namespace mcp
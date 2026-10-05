#include "McpMethodHandlers.h"
#include "mcp/transport/StreamableHttpMessageWriter.h"

namespace mcp
{
    McpMethodHandlers::McpMethodHandlers(MethodDispatcher &dispatcher)
        : subscriptions_(std::make_shared<SubscriptionRegistry>())
    {
        // Tools
        dispatcher.register_handler("tools/list",
                                    [this](auto &req, auto &)
                                    { return handle_tools_list(req); });
        dispatcher.register_handler("tools/call",
                                    [this](auto &req, auto &ctx)
                                    { return handle_tools_call(req, ctx); });

        // Resources
        dispatcher.register_handler("resources/list",
                                    [this](auto &req, auto &)
                                    { return handle_resources_list(req); });
        dispatcher.register_handler("resources/read",
                                    [this](auto &req, auto &)
                                    { return handle_resources_read(req); });
        dispatcher.register_handler("resources/templates/list",
                                    [this](auto &req, auto &)
                                    { return handle_resource_templates_list(req); });

        // Prompts
        dispatcher.register_handler("prompts/list",
                                    [this](auto &req, auto &)
                                    { return handle_prompts_list(req); });
        dispatcher.register_handler("prompts/get",
                                    [this](auto &req, auto &)
                                    { return handle_prompts_get(req); });

        dispatcher.register_handler("server/discover",
                                    [this](auto &req, auto &)
                                    {
                                        return handle_discover(req);
                                    });

        dispatcher.register_handler("subscriptions/listen",
                                    [this](auto &req, auto &ctx)
                                    {
                                        return handle_subscriptions_listen(req, ctx);
                                    });
    }

    // 供 McpServer 注入 discover 配置
    void McpMethodHandlers::set_discover_config(std::string server_name,
                                                std::string server_version,
                                                std::vector<std::string> versions,
                                                std::optional<std::string> instructions,
                                                int64_t ttl_ms)
    {
        check_not_frozen("set_discover_config");
        server_name_ = std::move(server_name);
        server_version_ = std::move(server_version);
        supported_versions_ = std::move(versions);
        instructions_ = std::move(instructions);
        discover_ttl_ms_ = ttl_ms;
    }

    SuccessResponse
    McpMethodHandlers::handle_discover(const Request &req)
    {
        // ---- 协议层校验：_meta 必须完整 ----
        auto meta = req.extract_meta();
        if (!meta || !meta->is_valid())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "Missing or invalid _meta in params");
        }

        // ---- 版本协商：确认客户端请求的版本在支持列表内 ----
        const std::string &client_version = meta->protocol_version;
        bool version_supported = std::find(
                                     supported_versions_.begin(),
                                     supported_versions_.end(),
                                     client_version) != supported_versions_.end();

        if (!version_supported)
        {
            // 规范定义了 UnsupportedProtocolVersionError，
            // 对应错误码 -32021
            throw McpException(
                ErrorCode::UnsupportedProtocolVersion,
                "Unsupported protocol version: " + client_version,
                json{{"supportedVersions", supported_versions_}});
        }

        // ---- 构建 capabilities ----
        json capabilities = json::object();

        // tools 能力：只要注册了至少一个工具即声明支持
        if (!tools_.empty())
        {
            capabilities["tools"] = json::object();
        }

        // resources 能力：静态资源或模板任一存在即声明支持
        if (!resources_.empty() || !resource_templates_.empty())
        {
            json resources_cap = json::object();

            // 如果注册了至少一个资源（静态或模板），
            // 就认为服务端可以处理 resourceSubscriptions 过滤器
            if (!resources_.empty() || !resource_templates_.empty())
            {
                resources_cap["subscribe"] = true;
            }
            capabilities["resources"] = std::move(resources_cap);
        }

        // prompts 能力
        if (!prompts_.empty())
        {
            capabilities["prompts"] = json::object();
        }

        // ---- 构建结果 ----
        json result;
        result["supportedVersions"] = supported_versions_;
        result["capabilities"] = std::move(capabilities);

        // serverInfo 位于 _meta 中，规范建议（SHOULD）包含
        result["_meta"] = json{
            {"io.modelcontextprotocol/serverInfo",
             json{{"name", server_name_}, {"version", server_version_}}}};

        // instructions 为可选字段
        if (instructions_)
        {
            result["instructions"] = *instructions_;
        }

        // 缓存提示：Discover 结果稳定，适合公共缓存
        result["ttlMs"] = discover_ttl_ms_;
        result["cacheScope"] = "public";

        return SuccessResponse::make(req.id, std::move(result));
    }

    void McpMethodHandlers::register_tool(ToolDescriptor t)
    {
        check_not_frozen("register_tool");
        tools_.emplace(t.name, std::move(t));
    }
    void McpMethodHandlers::register_resource(ResourceDescriptor r)
    {
        check_not_frozen("register_resource");
        resources_.emplace(r.uri, std::move(r));
    }
    void McpMethodHandlers::register_resource_template(ResourceTemplateDescriptor r)
    {
        check_not_frozen("register_resource_template");
        std::string key = r.uri_template;
        resource_templates_.emplace(
            key, CompiledTemplate{
                     UriTemplate(key),
                     std::move(r)});
    }
    void McpMethodHandlers::register_prompt(PromptDescriptor p)
    {
        check_not_frozen("register_prompt");
        prompts_.emplace(p.name, std::move(p));
    }

    SuccessResponse McpMethodHandlers::handle_tools_list(const Request &req)
    {
        // 提取分页游标（当前示例不支持分页，游标非空则返回错误）
        if (req.params && req.params->contains("cursor"))
        {
            // 生产实现应基于游标返回下一批，此处简化
            throw McpException(
                ErrorCode::InvalidParams,
                "Pagination not yet implemented");
        }

        // 确定性排序：按 name 字典序，确保每次响应一致
        std::vector<const ToolDescriptor *> sorted;
        for (const auto &[name, tool] : tools_)
            sorted.push_back(&tool);
        std::sort(sorted.begin(), sorted.end(),
                  [](const ToolDescriptor *a, const ToolDescriptor *b)
                  {
                      return a->name < b->name;
                  });

        json tools = json::array();
        for (const auto *t : sorted)
            tools.push_back(t->to_json());

        json result;
        result["tools"] = std::move(tools);
        // 缓存提示：工具列表通常稳定，可缓存 5 分钟，允许公共缓存
        result["ttlMs"] = 300000; // 5 分钟
        result["cacheScope"] = "public";

        return SuccessResponse::make(req.id, std::move(result));
    }

    SuccessResponse McpMethodHandlers::handle_tools_call(
        const Request &req, IRequestContext &ctx)
    {
        // ============================================================
        // 第一层：协议层校验（框架统一完成，失败返回 JSON-RPC error）
        // ============================================================
        auto meta = req.extract_meta();
        if (!meta || !meta->is_valid())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "Missing or invalid _meta in params");
        }
        if (!req.params || !(*req.params).is_object() ||
            !(*req.params).contains("name") ||
            !(*req.params)["name"].is_string())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "tools/call requires params.name");
        }

        std::string tool_name = (*req.params)["name"];
        auto it = tools_.find(tool_name);
        if (it == tools_.end())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "Unknown tool: " + tool_name);
        }

        // ============================================================
        // 第二层：结构层校验（框架自动完成，失败返回 JSON-RPC error）
        // ============================================================
        json args = req.params->value("arguments", json::object());
        if (!it->second.validator->validate(args))
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "Arguments do not match tool inputSchema",
                json{{"validation_errors",
                      it->second.validator->last_errors()}});
        }

        // ============================================================
        // 第三层：语义层 + 业务逻辑（业务方负责，失败返回 isError: true）
        // ============================================================
        try
        {
            ToolResult result = it->second.executor(args, ctx);
            return SuccessResponse::make(req.id, result.to_json());
        }
        catch (const std::exception &e)
        {
            // 语义错误 / 业务失败 → 转为工具执行错误，而非协议错误
            ToolResult err;
            err.content = json::array({{{"type", "text"}, {"text", e.what()}}});
            err.is_error = true;
            return SuccessResponse::make(req.id, err.to_json());
        }
    }

    SuccessResponse McpMethodHandlers::handle_resources_list(const Request &req)
    {
        // 确定性排序
        std::vector<const ResourceDescriptor *> sorted;
        for (const auto &[uri, r] : resources_)
            sorted.push_back(&r);
        std::sort(sorted.begin(), sorted.end(),
                  [](const ResourceDescriptor *a, const ResourceDescriptor *b)
                  {
                      return a->uri < b->uri;
                  });

        json arr = json::array();
        for (const auto *r : sorted)
            arr.push_back(r->to_json());

        json result;
        result["resources"] = std::move(arr);
        result["ttlMs"] = 60000;          // 资源列表可能频繁变化，缓存 1 分钟
        result["cacheScope"] = "private"; // 资源权限可能因授权而异
        return SuccessResponse::make(req.id, std::move(result));
    }

    SuccessResponse McpMethodHandlers::handle_resource_templates_list(
        const Request &req)
    {
        std::vector<const CompiledTemplate *> sorted;
        for (const auto &[tmpl, r] : resource_templates_)
            sorted.push_back(&r);
        std::sort(sorted.begin(), sorted.end(),
                  [](const CompiledTemplate *a,
                     const CompiledTemplate *b)
                  {
                      return a->descriptor.uri_template < b->descriptor.uri_template;
                  });

        json arr = json::array();
        for (const auto *r : sorted)
            arr.push_back(r->descriptor.to_json());

        json result;
        result["resourceTemplates"] = std::move(arr);
        result["ttlMs"] = 300000;
        result["cacheScope"] = "public";
        return SuccessResponse::make(req.id, std::move(result));
    }

    SuccessResponse McpMethodHandlers::handle_resources_read(const Request &req)
    {
        auto meta = req.extract_meta();
        if (!meta || !meta->is_valid())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "Missing or invalid _meta in params");
        }
        if (!req.params || !req.params->contains("uri") ||
            !(*req.params)["uri"].is_string())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "resources/read requires params.uri");
        }

        std::string uri = (*req.params)["uri"];

        // 先查静态资源
        auto it = resources_.find(uri);
        if (it != resources_.end())
        {
            json result;
            result["contents"] = it->second.reader();
            result["ttlMs"] = 30000;
            result["cacheScope"] = "private";
            return SuccessResponse::make(req.id, std::move(result));
        }

        // 2) 模板资源：编译后的模板直接 match
        for (const auto &[key, ct] : resource_templates_)
        {
            auto params = ct.compiled.match(uri);
            if (!params)
                continue;

            json result;
            result["contents"] = ct.descriptor.reader(uri, *params);
            result["ttlMs"] = 30000;
            result["cacheScope"] = "private";
            return SuccessResponse::make(req.id, std::move(result));
        }

        // 3) 都不匹配 → -32602（MCP 2026-07-28 将 resource-not-found 归入 InvalidParams）
        throw McpException(
            ErrorCode::InvalidParams,
            "Unknown resource URI: " + uri);
    }

    SuccessResponse McpMethodHandlers::handle_prompts_list(const Request &req)
    {
        std::vector<const PromptDescriptor *> sorted;
        for (const auto &[name, p] : prompts_)
            sorted.push_back(&p);
        std::sort(sorted.begin(), sorted.end(),
                  [](const PromptDescriptor *a, const PromptDescriptor *b)
                  {
                      return a->name < b->name;
                  });

        json arr = json::array();
        for (const auto *p : sorted)
            arr.push_back(p->to_json());

        json result;
        result["prompts"] = std::move(arr);
        result["ttlMs"] = 300000;
        result["cacheScope"] = "public";
        return SuccessResponse::make(req.id, std::move(result));
    }

    SuccessResponse McpMethodHandlers::handle_prompts_get(const Request &req)
    {
        auto meta = req.extract_meta();
        if (!meta || !meta->is_valid())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "Missing or invalid _meta in params");
        }
        if (!req.params || !req.params->contains("name") ||
            !(*req.params)["name"].is_string())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "prompts/get requires params.name");
        }

        std::string name = (*req.params)["name"];
        auto it = prompts_.find(name);
        if (it == prompts_.end())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "Unknown prompt: " + name);
        }

        // 参数校验：检查必填参数是否全部提供
        json args = req.params->value("arguments", json::object());
        for (const auto &arg : it->second.arguments)
        {
            if (arg.required && !args.contains(arg.name))
            {
                throw McpException(
                    ErrorCode::InvalidParams,
                    "Missing required argument: " + arg.name);
            }
        }

        // 渲染消息序列
        json messages = it->second.renderer(args);

        json result;
        result["messages"] = std::move(messages);
        result["ttlMs"] = 0; // prompt 内容通常不缓存
        result["cacheScope"] = "private";
        return SuccessResponse::make(req.id, std::move(result));
    }

    HandlerResult McpMethodHandlers::handle_subscriptions_listen(
        const Request &req,
        IRequestContext &ctx)
    {
        // ---- 协议层校验 ----
        auto meta = req.extract_meta();
        if (!meta || !meta->is_valid())
        {
            throw McpException(
                ErrorCode::InvalidParams,
                "Missing or invalid _meta in params");
        }

        // ---- 解析过滤器 ----
        json notifications_raw = json::object();
        if (req.params && req.params->is_object() &&
            req.params->contains("notifications"))
        {
            notifications_raw = (*req.params)["notifications"];
        }
        SubscriptionFilter requested =
            SubscriptionFilter::from_json(notifications_raw);

        // ---- 确定服务端实际同意履行的子集 ----
        SubscriptionFilter honored;

        // 冻结架构下，注册表不会变化，因此不推送 list_changed 通知。
        // 如果将来解冻，这里应改为 true（前提是注册了对应能力）。
        // 当前如实返回 false（省略字段），让客户端知道不会收到变更通知。

        // 资源订阅：只要请求了 URI 且资源存在，就同意
        for (const auto &uri : requested.resource_subscriptions)
        {
            // if (resource_exists(uri))
            // 先查静态资源
            if (resources_.find(uri) != resources_.end())
            {
                honored.resource_subscriptions.push_back(uri);
            }
            else
            {
                // 模板资源：编译后的模板直接 match
                for (const auto &[key, ct] : resource_templates_)
                {
                    if (ct.compiled.match(uri).has_value())
                    {
                        honored.resource_subscriptions.push_back(uri);
                    }
                }
            }
        }

        // ---- 构建 acknowledged 通知 ----
        json ack_notifications = json::object();
        if (honored.tools_list_changed)
            ack_notifications["toolsListChanged"] = true;
        if (honored.prompts_list_changed)
            ack_notifications["promptsListChanged"] = true;
        if (honored.resources_list_changed)
            ack_notifications["resourcesListChanged"] = true;
        if (!honored.resource_subscriptions.empty())
            ack_notifications["resourceSubscriptions"] =
                honored.resource_subscriptions;

        std::string subscription_id = req.id.to_string();

        json ack;
        ack["jsonrpc"] = "2.0";
        ack["method"] = METHOD_SUBSCRIPTIONS_ACKNOWLEDGED;
        ack["params"] = {
            {"_meta", {{KEY_SUBSCRIPTION_ID, subscription_id}}},
            {"notifications", std::move(ack_notifications)}};

        // 通过 writer 直接发送 acknowledged（这是流上的第一条消息）
        ctx.writer().write_notification(ack);

        subscriptions_->add(subscription_id,
                            std::move(honored),
                            ctx.shared_writer());

        auto *http_writer = dynamic_cast<StreamableHttpMessageWriter *>(&ctx.writer());
        if (http_writer)
        {
            std::weak_ptr<SubscriptionRegistry> weak_registry = subscriptions_;
            http_writer->set_subscription_cleanup(subscription_id,
                                                  [weak_registry](const std::string &id)
                                                  {
                                                      if (auto reg = weak_registry.lock())
                                                          reg->remove(id);
                                                  });
        }

        return StreamOpenedTag{};
    }
}
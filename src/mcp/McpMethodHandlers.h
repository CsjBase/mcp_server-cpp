#pragma once

#include "json_rpc/MethodDispatcher.h"

#include "ToolDescriptor.h"

namespace mcp
{

    class McpMethodHandlers
    {
    public:
        explicit McpMethodHandlers(MethodDispatcher &dispatcher)
        {
            dispatcher.register_handler("tools/call",
                                        [this](const Request &req, IRequestContext &ctx)
                                        {
                                            return handle_tools_call(req, ctx);
                                        });
        }

        void register_tool(ToolDescriptor tool)
        {
            tools_.emplace(tool.name, std::move(tool));
        }

    private:
        SuccessResponse handle_tools_call(
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
            if (!req.params || !req.params->is_object())
            {
                throw McpException(
                    ErrorCode::InvalidParams,
                    "params must be an object");
            }
            if (!req.params->contains("name") ||
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
                json output = it->second.executor(args, ctx);

                json result;
                result["content"] = std::move(output);
                return SuccessResponse::make(
                    req.id, std::move(result));
            }
            catch (const std::exception &e)
            {
                // 语义错误 / 业务失败 → 转为工具执行错误，而非协议错误
                json result;
                result["content"] = json::array({{{"type", "text"}, {"text", e.what()}}});
                result["isError"] = true;
                return SuccessResponse::make(
                    req.id, std::move(result));
            }
        }

        std::unordered_map<std::string, ToolDescriptor> tools_;
    };

} // namespace mcp
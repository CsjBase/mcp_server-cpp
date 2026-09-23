#pragma once

#include "mcp/json_rpc/JsonRpcContext.h"
#include "mcp/json_rpc/InputValidator.h"

#include <functional>
#include <memory>

namespace mcp
{
    // 工具执行结果：文本内容 + 可选的结构化输出
    struct ToolResult
    {
        json content;                           // [{"type":"text","text":"..."}]
        std::optional<json> structured_content; // 与 outputSchema 对应
        bool is_error = false;

        json to_json() const
        {
            json r = {{"content", content}};
            if (structured_content)
                r["structuredContent"] = *structured_content;
            if (is_error)
                r["isError"] = true;
            return r;
        }
    };

    // 工具执行器签名：args 已保证符合 inputSchema，ctx 用于主动上报
    using ToolExecutor = std::function<ToolResult(
        const json &args, IRequestContext &ctx)>;

    struct ToolDescriptor
    {
        std::string name;
        std::string description;
        std::shared_ptr<InputValidator> validator;
        std::optional<json> output_schema; // 声明结构化输出的形状

        // 业务逻辑：入参保证结构合法，只需处理语义
        ToolExecutor executor;

        json to_json() const
        {
            json j = {
                {"name", name},
                {"description", description},
                {"inputSchema", validator->schema()}};
            if (output_schema)
                j["outputSchema"] = *output_schema;
            return j;
        }

        static ToolDescriptor make(
            std::string name,
            std::string description,
            json input_schema,
            ToolExecutor executor,
            std::optional<json> output_schema = std::nullopt)
        {
            return ToolDescriptor{
                std::move(name),
                std::move(description),
                std::make_shared<InputValidator>(std::move(input_schema)),
                std::move(output_schema),
                std::move(executor)};
        }
    };

} // namespace mcp
#pragma once

#include "json_rpc/JsonRpcContext.h"
#include "InputValidator.h"

#include <functional>
#include <memory>

namespace mcp
{

    // 工具执行器签名：args 已保证符合 inputSchema，ctx 用于主动上报
    using ToolExecutor = std::function<json(
        const json &args, IRequestContext &ctx)>;

    struct ToolDescriptor
    {
        std::string name;
        std::string description;
        std::shared_ptr<InputValidator> validator;

        // 业务逻辑：入参保证结构合法，只需处理语义
        ToolExecutor executor;

        json to_json() const
        {
            return {
                {"name", name},
                {"description", description},
                {"inputSchema", validator->schema()}};
        }

        static ToolDescriptor make(
            std::string name,
            std::string description,
            json input_schema,
            ToolExecutor executor)
        {
            return ToolDescriptor{
                std::move(name),
                std::move(description),
                std::make_shared<InputValidator>(std::move(input_schema)),
                std::move(executor)};
        }
    };

} // namespace mcp
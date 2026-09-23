#pragma once

#include <optional>
#include <functional>
#include <nlohmann/json.hpp>

namespace mcp
{

    using json = nlohmann::json;

    struct PromptArgument
    {
        std::string name;
        std::string description;
        bool required = false;

        json to_json() const
        {
            return {{"name", name},
                    {"description", description},
                    {"required", required}};
        }
    };

    struct PromptDescriptor
    {
        std::string name;
        std::optional<std::string> description;
        std::vector<PromptArgument> arguments;

        // 根据参数生成消息序列
        std::function<json(const json &args)> renderer;

        json to_json() const
        {
            json j = {{"name", name}};
            if (description)
                j["description"] = *description;
            if (!arguments.empty())
            {
                json arr = json::array();
                for (const auto &a : arguments)
                    arr.push_back(a.to_json());
                j["arguments"] = std::move(arr);
            }
            return j;
        }
    };

} // namespace mcp
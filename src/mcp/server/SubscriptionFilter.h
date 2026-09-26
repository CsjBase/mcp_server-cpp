#pragma once
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace mcp
{

    using json = nlohmann::json;

    // 通知方法名常量（2026-07-28 使用下划线，非驼峰）
    inline constexpr const char *METHOD_TOOLS_LIST_CHANGED =
        "notifications/tools/list_changed";
    inline constexpr const char *METHOD_PROMPTS_LIST_CHANGED =
        "notifications/prompts/list_changed";
    inline constexpr const char *METHOD_RESOURCES_LIST_CHANGED =
        "notifications/resources/list_changed";
    inline constexpr const char *METHOD_RESOURCES_UPDATED =
        "notifications/resources/updated";

    inline constexpr const char *METHOD_SUBSCRIPTIONS_ACKNOWLEDGED =
        "notifications/subscriptions/acknowledged";

    inline constexpr const char *KEY_SUBSCRIPTION_ID =
        "io.modelcontextprotocol/subscriptionId";

    // 客户端请求的过滤器
    struct SubscriptionFilter
    {
        bool tools_list_changed = false;
        bool prompts_list_changed = false;
        bool resources_list_changed = false;
        std::vector<std::string> resource_subscriptions; // 资源 URI 列表

        static SubscriptionFilter from_json(const json &j)
        {
            SubscriptionFilter f;
            f.tools_list_changed =
                j.value("toolsListChanged", false);
            f.prompts_list_changed =
                j.value("promptsListChanged", false);
            f.resources_list_changed =
                j.value("resourcesListChanged", false);
            if (j.contains("resourceSubscriptions") &&
                j["resourceSubscriptions"].is_array())
            {
                for (const auto &uri : j["resourceSubscriptions"])
                {
                    if (uri.is_string())
                        f.resource_subscriptions.push_back(uri);
                }
            }
            return f;
        }

        bool is_empty() const
        {
            return !tools_list_changed && !prompts_list_changed &&
                   !resources_list_changed &&
                   resource_subscriptions.empty();
        }
    };

} // namespace mcp
// include/mcp/server/SubscriptionRegistry.h
#pragma once
#include "mcp/server/SubscriptionFilter.h"
#include "mcp/json_rpc/JsonRpcContext.h"
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <mutex>

namespace mcp
{

    // 一条活跃订阅的记录
    struct SubscriptionEntry
    {
        std::string subscription_id;          // listen 请求的 JSON-RPC ID
        SubscriptionFilter honored_filter;    // 服务端实际同意履行的子集
        std::weak_ptr<IMessageWriter> writer; // 弱引用，不延长连接寿命

        bool honored_filter_matches(const std::string &method) const
        {
            if (method == METHOD_TOOLS_LIST_CHANGED)
                return honored_filter.tools_list_changed;
            if (method == METHOD_PROMPTS_LIST_CHANGED)
                return honored_filter.prompts_list_changed;
            if (method == METHOD_RESOURCES_LIST_CHANGED)
                return honored_filter.resources_list_changed;
            if (method == METHOD_RESOURCES_UPDATED)
                return !honored_filter.resource_subscriptions.empty();
            return false;
        }
    };

    class SubscriptionRegistry
    {
    public:
        // 注册一条订阅。返回是否成功。
        bool add(const std::string &subscription_id,
                 SubscriptionFilter honored_filter,
                 std::shared_ptr<IMessageWriter> writer)
        {
            std::lock_guard lock(mutex_);
            SubscriptionEntry entry{
                subscription_id,
                std::move(honored_filter),
                writer};
            return subscriptions_.emplace(subscription_id,
                                          std::move(entry))
                .second;
        }

        // 移除一条订阅（流关闭或客户端取消时调用）
        void remove(const std::string &subscription_id)
        {
            std::lock_guard lock(mutex_);
            subscriptions_.erase(subscription_id);
        }

        // 向所有订阅了指定通知类型的流推送通知。
        // 在冻结架构下，这个方法不会被调用；
        // 解冻注册表后，register_tool 等路径会调用它。
        void notify(const std::string &method,
                    const json &params_without_meta)
        {
            std::lock_guard lock(mutex_);
            for (auto &[id, entry] : subscriptions_)
            {
                if (!entry.honored_filter_matches(method))
                    continue;
                auto writer = entry.writer.lock();
                if (!writer)
                    continue; // 连接已断开

                json notification;
                notification["jsonrpc"] = "2.0";
                notification["method"] = method;

                json params = params_without_meta;
                params["_meta"][KEY_SUBSCRIPTION_ID] = entry.subscription_id;
                notification["params"] = std::move(params);

                writer->write_notification(notification);
            }
        }

        size_t size() const
        {
            std::lock_guard lock(mutex_);
            return subscriptions_.size();
        }

    private:
        std::unordered_map<std::string, SubscriptionEntry> subscriptions_;
        mutable std::mutex mutex_;
    };

} // namespace mcp
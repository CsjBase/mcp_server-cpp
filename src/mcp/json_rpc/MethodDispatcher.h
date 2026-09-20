#pragma once

#include "JsonRpcTypes.h"
#include "DefaultRequestContext.h"

#include <functional>
#include <unordered_map>
#include <shared_mutex>
#include <regex>
#include <mutex>

namespace mcp
{

    // 请求处理器签名：接收 Request 和 Context
    using RequestHandler = std::function<
        std::variant<SuccessResponse, ErrorResponse>(
            const Request &, IRequestContext &)>;

    using NotificationHandler = std::function<void(const Notification &)>;

    class MethodDispatcher
    {
    public:
        bool register_handler(const std::string &method, RequestHandler handler);

        bool register_notification(const std::string &method,
                                   NotificationHandler handler);

        // 核心分发入口：接收原始 JSON 字符串和传输层写入器
        std::optional<json> dispatch(const std::string &raw,
                                     std::shared_ptr<IMessageWriter> writer);

    private:
        std::optional<json> dispatch_single(
            const json &j, std::shared_ptr<IMessageWriter> writer);

        json handle_request(const Request &req,
                            std::shared_ptr<IMessageWriter> writer);

        std::unique_ptr<IRequestContext> make_context(
            const Request &req,
            std::shared_ptr<IMessageWriter> writer);

        void handle_notification(const Notification &notif);

        std::unordered_map<std::string, RequestHandler> handlers_;
        std::unordered_map<std::string, NotificationHandler> notification_handlers_;
        std::shared_mutex mutex_;
    };

} // namespace mcp
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

    // dispatch() 的返回类型：表达三种可能的执行结果。
    // 与 HandlerResult 的区别在于，payload 已经序列化为 json，
    // 传输层可以直接写入连接。
    struct DispatchOutcome
    {
        enum class Kind
        {
            Response,     // 有响应，payload 包含序列化后的 JSON
            Notification, // 通知，无响应
            StreamOpened  // 流已打开，连接保持，无响应
        };

        Kind kind;
        std::optional<json> payload; // 仅 Kind::Response 时有值

        static DispatchOutcome make_response(json r)
        {
            return {Kind::Response, std::move(r)};
        }
        static DispatchOutcome make_notification()
        {
            return {Kind::Notification, std::nullopt};
        }
        static DispatchOutcome make_stream_opened()
        {
            return {Kind::StreamOpened, std::nullopt};
        }
    };

    // handler 的返回类型：三态 variant。
    // 现有的 SuccessResponse / ErrorResponse handler 无需修改——
    // 它们会被隐式转换到这个 variant。
    using HandlerResult = std::variant<
        SuccessResponse, ErrorResponse, StreamOpenedTag>;

    // 请求处理器签名：接收 Request 和 Context
    using RequestHandler = std::function<
        HandlerResult(
            const Request &, IRequestContext &)>;

    using NotificationHandler = std::function<void(const Notification &)>;

    class MethodDispatcher
    {
    public:
        bool register_handler(const std::string &method, RequestHandler handler);

        bool register_notification(const std::string &method,
                                   NotificationHandler handler);

        // 核心分发入口：接收原始 JSON 字符串和传输层写入器
        DispatchOutcome dispatch(const std::string &raw,
                                 std::shared_ptr<IMessageWriter> writer);

    private:
        DispatchOutcome dispatch_single(
            const json &j, std::shared_ptr<IMessageWriter> writer);

        DispatchOutcome handle_request(const Request &req,
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
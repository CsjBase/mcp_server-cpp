#include "MethodDispatcher.h"
#include "mcp/base/Log.h"

namespace mcp
{
    bool MethodDispatcher::register_handler(const std::string &method, RequestHandler handler)
    {
        std::unique_lock lock(mutex_);
        return handlers_.emplace(method, std::move(handler)).second;
    }

    bool MethodDispatcher::register_notification(const std::string &method,
                                                 NotificationHandler handler)
    {
        std::unique_lock lock(mutex_);
        return notification_handlers_.emplace(method,
                                              std::move(handler))
            .second;
    }

    DispatchOutcome MethodDispatcher::dispatch(const std::string &raw,
                                               std::shared_ptr<IMessageWriter> writer)
    {
        json j;
        try
        {
            j = json::parse(raw);
        }
        catch (const json::parse_error &)
        {
            MCP_LOG_WARN("Failed to parse JSON: {}", raw);
            return DispatchOutcome::make_response(
                make_error(ErrorCode::ParseError).to_json());
        }

        // MCP 2026-07-28 移除了批量请求，明确拒绝
        if (j.is_array())
        {
            MCP_LOG_WARN("Batch request has been removed, illegal array input received");
            return DispatchOutcome::make_response(
                make_error(ErrorCode::InvalidRequest, std::nullopt,
                           "Batch requests are not supported in MCP 2026-07-28")
                    .to_json());
        }

        return dispatch_single(j, std::move(writer));
    }

    DispatchOutcome MethodDispatcher::dispatch_single(
        const json &j, std::shared_ptr<IMessageWriter> writer)
    {
        if (j.contains("method"))
        {
            bool has_id = j.contains("id") && !j["id"].is_null();
            if (has_id)
            {
                auto req = Request::from_json(j);
                if (!req)
                {
                    MCP_LOG_WARN("Received invalid request");
                    return DispatchOutcome::make_response(
                        make_error(ErrorCode::InvalidRequest).to_json());
                }
                return handle_request(*req, std::move(writer));
            }
            else
            {
                auto notif = Notification::from_json(j);
                if (notif)
                    handle_notification(*notif);
                return DispatchOutcome::make_notification();
            }
        }
        return DispatchOutcome::make_response(
            make_error(ErrorCode::InvalidRequest).to_json());
    }

    DispatchOutcome MethodDispatcher::handle_request(const Request &req,
                                                     std::shared_ptr<IMessageWriter> writer)
    {
        RequestHandler handler;
        {
            std::shared_lock lock(mutex_);
            auto it = handlers_.find(req.method);
            if (it == handlers_.end())
            {
                MCP_LOG_WARN("Request method not found. method:{}, requstid:{}",
                             req.method, req.id.to_string());
                return DispatchOutcome::make_response(
                    make_error(ErrorCode::MethodNotFound,
                               req.id, req.method)
                        .to_json());
            }
            handler = it->second;
        }

        // // 2026-07-28 `Logging` 特性已进入 12 个月弃用窗口
        // if (req.params && req.params->is_object() &&
        //     req.params->contains("_meta") &&
        //     (*req.params)["_meta"].is_object() &&
        //     (*req.params)["_meta"].contains(RequestMeta::KEY_LOG_LEVEL))
        // {
        //     MCP_LOG_DEBUG(
        //         "client requested deprecated logLevel, rejecting | request_id={}",
        //         req.id.to_string());
        //     return make_error(
        //                ErrorCode::InvalidParams,
        //                req.id,
        //                "logLevel is deprecated (SEP-2577); "
        //                "logs are written to stderr / OpenTelemetry, "
        //                "not sent over the protocol")
        //         .to_json();
        // }

        // ---- 构造请求上下文，注入上报能力 ----
        auto ctx = make_context(req, writer);

        try
        {
            auto result = handler(req, *ctx);
            return std::visit(
                [](auto &&r) -> DispatchOutcome
                {
                    using T = std::decay_t<decltype(r)>;
                    if constexpr (std::is_same_v<T, StreamOpenedTag>)
                    {
                        return DispatchOutcome::make_stream_opened();
                    }
                    else
                    {
                        return DispatchOutcome::make_response(r.to_json());
                    }
                },
                std::move(result));
        }
        catch (const McpException &e)
        {
            ErrorObject err;
            err.code = static_cast<int>(e.code());
            err.message = e.what();
            if (e.data())
                err.data = *e.data();

            MCP_LOG_WARN("Request method failed. err_code:{}, err_msg:{}, method:{}, requstid:{}", err.code, err.message, req.method, req.id.to_string());
            return DispatchOutcome::make_response(
                ErrorResponse{req.id, std::move(err)}.to_json());
        }
        catch (const std::exception &e)
        {
            MCP_LOG_ERROR("Request method failed. err_msg:{}, method:{}, requstid:{}", e.what(), req.method, req.id.to_string());
            return DispatchOutcome::make_response(
                make_error(ErrorCode::InternalError,
                           req.id, e.what())
                    .to_json());
        }
    }

    std::unique_ptr<IRequestContext> MethodDispatcher::make_context(
        const Request &req,
        std::shared_ptr<IMessageWriter> writer)
    {
        RequestMeta meta;
        if (auto m = req.extract_meta())
        {
            meta = *m;
        }

        std::optional<ProgressToken> progress_token;
        if (meta.progress_token)
        {
            const auto &pt = *meta.progress_token;
            if (pt.is_string())
            {
                progress_token = pt.get<std::string>();
            }
            else if (pt.is_number_integer())
            {
                progress_token = pt.get<int64_t>();
            }
        }

        return std::make_unique<DefaultRequestContext>(
            std::move(meta), std::move(progress_token), std::move(writer));
    }

    void MethodDispatcher::handle_notification(const Notification &notif)
    {
        std::shared_lock lock(mutex_);
        auto it = notification_handlers_.find(notif.method);
        if (it != notification_handlers_.end())
        {
            try
            {
                it->second(notif);
            }
            catch (...)
            { /* 通知静默失败 */
                // MCP_LOG
            }
        }
    }
}
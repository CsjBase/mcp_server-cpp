#include "DefaultRequestContext.h"

namespace mcp
{
    void DefaultRequestContext::report_progress(
        double progress,
        std::optional<double> total,
        std::optional<std::string> message)
    {
        if (!progress_token_ || !writer_)
        {
            return; // 客户端未请求进度 → no-op
        }

        json notification;
        notification["jsonrpc"] = JSONRPC_VERSION;
        notification["method"] = "notifications/progress";

        json params;
        params["progressToken"] = std::visit(
            [](const auto &v) -> json
            { return v; }, *progress_token_);
        params["progress"] = progress;
        if (total)
            params["total"] = *total;
        if (message)
            params["message"] = *message;

        notification["params"] = std::move(params);
        writer_->write_notification(notification);
    }

}
#pragma once

#include "JsonRpcContext.h"

#include <memory>

namespace mcp
{
    using json = nlohmann::json;

    // 进度令牌：字符串或整数，与 RequestId 同构
    using ProgressToken = std::variant<std::string, int64_t>;

    class DefaultRequestContext : public IRequestContext
    {
    public:
        DefaultRequestContext(
            RequestMeta meta,
            std::optional<ProgressToken> progress_token,
            std::shared_ptr<IMessageWriter> writer)
            : meta_(std::move(meta)), progress_token_(std::move(progress_token)), writer_(std::move(writer)) {}

        void report_progress(
            double progress,
            std::optional<double> total = std::nullopt,
            std::optional<std::string> message = std::nullopt) override;

        const RequestMeta &meta() const override { return meta_; }

        bool has_progress_token() const override
        {
            return progress_token_.has_value();
        }

        IMessageWriter &writer() override
        {
            return *writer_;
        }

        std::shared_ptr<IMessageWriter> shared_writer() override
        {
            return writer_;
        }

    private:
        RequestMeta meta_;
        std::optional<ProgressToken> progress_token_;
        std::shared_ptr<IMessageWriter> writer_;
    };

} // namespace mcp
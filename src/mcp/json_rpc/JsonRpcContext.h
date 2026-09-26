#pragma once

#include "JsonRpcTypes.h"

namespace mcp
{

    // 传输层写入抽象：负责将通知和最终响应写入当前请求的响应流
    class IMessageWriter
    {
    public:
        virtual ~IMessageWriter() = default;

        // 发送请求作用域通知（notifications/progress 等）
        virtual void write_notification(const json &notification) = 0;

        // 写入最终响应并关闭流
        virtual void write_response(const json &response) = 0;

        // 当前是否处于 SSE 流模式
        virtual bool is_streaming() const = 0;
    };

    // 请求上下文：业务方通过它主动上报，无需感知具体传输实现
    class IRequestContext
    {
    public:
        virtual ~IRequestContext() = default;

        // 发送进度通知。如果客户端未提供 progressToken，此调用为 no-op。
        virtual void report_progress(
            double progress,
            std::optional<double> total = std::nullopt,
            std::optional<std::string> message = std::nullopt) = 0;

        // 请求的元数据，便于业务方做条件判断
        virtual const RequestMeta &meta() const = 0;

        // 客户端是否请求了进度上报
        virtual bool has_progress_token() const = 0;

        // 获取底层 writer，用于订阅流等需要直接发送通知的场景。
        // 普通工具不应调用此方法。
        virtual IMessageWriter &writer() = 0;
        virtual std::shared_ptr<IMessageWriter> shared_writer() = 0;
    };

}
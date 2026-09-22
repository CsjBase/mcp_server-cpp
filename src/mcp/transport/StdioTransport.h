#pragma once

#include "ITransport.h"
#include <atomic>
#include <mutex>

namespace mcp
{

    class StdioTransport : public ITransport
    {
    public:
        StdioTransport() = default;
        ~StdioTransport() override;

        void set_handler(MessageHandler handler) override;

        // 阻塞运行，直到 stdin 关闭
        void start() override;

        // 由信号处理器或外部线程调用，触发优雅退出
        void stop() override;

        // 写入一行 JSON-RPC 消息到 stdout
        // 线程安全：多个 worker 线程可能同时上报进度
        void send(const std::string &raw) override;

    private:
        // 读取 stdin 的一行。返回 false 表示 EOF 或错误。
        bool read_line(std::string &out);

        MessageHandler handler_;
        std::mutex write_mutex_; // 保护 stdout 写入
        std::atomic<bool> running_{false};
        std::atomic<bool> eof_{false};
    };

} // namespace mcp
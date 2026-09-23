// include/mcp/server/McpServer.h
#pragma once

// #include "mcp/json_rpc/MethodDispatcher.h"
// #include "mcp/server/McpMethodHandlers.h"
#include "mcp/server/ToolDescriptor.h"
#include "mcp/server/ResourceDescriptor.h"
#include "mcp/server/PromptDescriptor.h"

#include <memory>
#include <string>
#include <atomic>

namespace mcp
{
    struct Options
    {
        std::string name = "mcp-server";
        std::string version = "1.0.0";
        // IO 线程数（HTTP 传输使用，stdio 不使用）
        int io_threads = 4;

        // 业务线程池大小。0 表示同步执行（仅用于测试）。
        int business_threads = 4;

        // 业务线程池队列上限。超出时新请求返回 -32603。
        size_t business_queue_limit = 4096;

        // SSE keepalive 间隔（秒）。<= 0 表示禁用心跳。
        double sse_keepalive_interval = 30.0;

        // ---- server/discover 相关 ----
        // 服务端支持的协议版本列表。默认仅支持当前规范版本。
        std::vector<std::string> supported_versions = {"2026-07-28"};
        // Discover 结果的缓存时长（毫秒）。规范建议公共缓存 1 小时。
        int64_t discover_ttl_ms = 3600000; // 1 hour
        // 可选的 instructions 字段：给 LLM 的自然语言使用指引
        std::optional<std::string> instructions = std::nullopt;
    };
    class McpServer
    {
    public:
        explicit McpServer(Options opts = {});
        ~McpServer();

        McpServer(const McpServer &) = delete;
        McpServer &operator=(const McpServer &) = delete;

        // ---- 能力注册。必须在 start_* 之前调用。 ----
        void register_tool(ToolDescriptor tool);
        void register_resource(ResourceDescriptor resource);
        void register_resource_template(ResourceTemplateDescriptor tmpl);
        void register_prompt(PromptDescriptor prompt);

        // ---- 传输启动。二者互斥，只能选一个。 ----
        void listen_http(const std::string &addr, uint16_t port);
        void run_stdio();

        const Options &options() const { return opts_; }

    private:
        // 从 Options 构造的共享资源
        struct Impl;
        std::unique_ptr<Impl> impl_;
        Options opts_;
        std::atomic<bool> started_{false};
    };

} // namespace mcp
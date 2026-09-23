#include "McpServer.h"

#include "mcp/json_rpc/MethodDispatcher.h"
#include "net/EventLoop.h"
#include "utils/thread_pool.h"
#include "mcp/transport/StdioTransport.h"
#include "mcp/transport/StdioMessageWriter.h"
#include "mcp/transport/StreamableHttpServer.h"
#include "mcp/server/McpMethodHandlers.h"
#include "mcp/base/Log.h"
#include "mcp/transport/IExecutor.h"

namespace mcp
{

    // ============================================================
    // Impl：把所有内部组件放在这里，头文件不暴露实现细节
    // ============================================================
    struct McpServer::Impl
    {
        MethodDispatcher dispatcher;
        std::unique_ptr<McpMethodHandlers> handlers;
        std::unique_ptr<IExecutor> executor;

        // 传输层。同一时刻只有一个非空。
        std::unique_ptr<net::EventLoop> loop;
        std::shared_ptr<StreamableHttpServer> http_server;
        std::unique_ptr<StdioTransport> stdio_transport;
    };

    McpServer::McpServer(Options opts)
        : impl_(std::make_unique<Impl>()), opts_(std::move(opts))
    {
        impl_->handlers = std::make_unique<McpMethodHandlers>(impl_->dispatcher);
        impl_->handlers->set_discover_config(opts_.name, opts_.version,
                                             opts_.supported_versions, opts_.instructions,
                                             opts_.discover_ttl_ms);

        // 业务线程池：仅在配置为正数时创建。
        // 测试可传 business_threads = 0，此时 dispatch 在调用线程中同步执行。
        if (opts_.business_threads > 0)
        {
            impl_->executor = std::make_unique<ThreadPoolExecutor>(opts_.business_threads, opts_.business_queue_limit);
            MCP_LOG_INFO("McpServer '{}' v{}: ThreadPoolExecutor started, threads={}, "
                         "queue_limit={}",
                         opts_.name, opts_.version,
                         opts_.business_threads, opts_.business_queue_limit);
        }
        else
        {
            impl_->executor = std::make_unique<SynchronousExecutor>();
            MCP_LOG_INFO("McpServer '{}' v{}: start in synchronous mode",
                         opts_.name, opts_.version);
        }
    }

    McpServer::~McpServer()
    {
    }

    // ============================================================
    // 能力注册：转发给 McpMethodHandlers
    // ============================================================
    void McpServer::register_tool(ToolDescriptor tool)
    {
        if (started_)
        {
            throw std::logic_error(
                "register_tool() must be called before listen_http/run_stdio");
        }
        impl_->handlers->register_tool(std::move(tool));
    }

    void McpServer::register_resource(ResourceDescriptor resource)
    {
        if (started_)
        {
            throw std::logic_error(
                "register_resource() must be called before listen_http/run_stdio");
        }
        impl_->handlers->register_resource(std::move(resource));
    }

    void McpServer::register_resource_template(ResourceTemplateDescriptor tmpl)
    {
        if (started_)
        {
            throw std::logic_error(
                "register_resource_template() must be called before "
                "listen_http/run_stdio");
        }
        impl_->handlers->register_resource_template(std::move(tmpl));
    }

    void McpServer::register_prompt(PromptDescriptor prompt)
    {
        if (started_)
        {
            throw std::logic_error(
                "register_prompt() must be called before listen_http/run_stdio");
        }
        impl_->handlers->register_prompt(std::move(prompt));
    }

    // ============================================================
    // HTTP 传输
    // ============================================================
    void McpServer::listen_http(const std::string &addr, uint16_t port)
    {
        if (started_.exchange(true))
        {
            throw std::logic_error(
                "listen_http() or run_stdio() already called");
        }

        // 能力注册完成，冻结注册表。此后任何 register_* 调用都会抛异常。
        impl_->handlers->freeze();

        impl_->loop = std::make_unique<net::EventLoop>();

        auto listen_addr = std::make_shared<net::InetAddress>(addr, port);
        impl_->http_server = std::make_shared<StreamableHttpServer>(
            impl_->loop.get(),
            listen_addr,
            &impl_->dispatcher,
            *impl_->executor);
        impl_->http_server->setKeepaliveInterval(opts_.sse_keepalive_interval);
        impl_->http_server->start(opts_.io_threads);

        MCP_LOG_INFO("McpServer '{}' v{} listening on {}:{}",
                     opts_.name, opts_.version, addr, port);

        impl_->loop->loop(); // 阻塞
    }

    // ============================================================
    // stdio 传输
    // ============================================================
    void McpServer::run_stdio()
    {
        if (started_.exchange(true))
        {
            throw std::logic_error(
                "listen_http() or run_stdio() already called");
        }

        impl_->handlers->freeze();

        impl_->stdio_transport =
            std::make_unique<StdioTransport>();

        // stdio 只有一个输出通道，writer 是共享的无状态对象。
        // 不同于 HTTP 的 per-request writer。
        auto writer = std::make_shared<StdioMessageWriter>(
            *impl_->stdio_transport);

        impl_->stdio_transport->set_handler(
            [this, writer](const std::string &raw)
                -> std::optional<std::string>
            {
                // stdio 是单线程串行模型，业务线程池在这里用不上。
                // 若配置了线程池，仍可在 handler 内部使用，但当前
                // 直接同步调用 dispatch。
                auto result = impl_->dispatcher.dispatch(raw, writer);
                if (result.has_value())
                {
                    return result->dump();
                }
                return std::nullopt; // 通知：无响应
            });

        MCP_LOG_INFO("McpServer '{}' v{} running on stdio",
                     opts_.name, opts_.version);

        impl_->stdio_transport->start(); // 阻塞至 stdin EOF
    }

} // namespace mcp
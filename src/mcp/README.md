# mcp — MCP 协议服务器

实现 [MCP（Model Context Protocol）2026-07-28 规范](https://modelcontextprotocol.io) 的 JSON-RPC 2.0 服务器：工具（tools）、资源（resources）、提示词（prompts）三大能力注册，`subscriptions/listen` 订阅机制，支持 **stdio** 与 **Streamable HTTP** 两种传输。HTTP 传输复用本项目自建 [net](../net/README.md) 网络库，日志走 [logger](../logger/README.md)。

## 目录结构

```
mcp/
├── McpServer.h / .cc       # 门面类：注册能力 + 启动传输（唯一对外入口）
├── json_rpc/               # JSON-RPC 2.0 协议层
│   ├── JsonRpcTypes.h      # 请求/响应/错误对象、标准错误码
│   ├── MethodDispatcher.h  # 方法名 → 处理器分发（含通知/请求区分）
│   ├── InputValidator.h    # JSON Schema 校验（工具入参、订阅过滤器）
│   ├── JsonRpcContext.h    # 会话上下文（客户端元数据、传输能力）
│   └── DefaultRequestContext.h
├── server/                 # 能力模型与业务处理
│   ├── ToolDescriptor.h    # 工具描述：name/description/schema/executor
│   ├── ResourceDescriptor.h# 资源与资源模板（URI / URI Template）
│   ├── PromptDescriptor.h  # 提示词描述
│   ├── McpMethodHandlers.h # initialize、tools/list、tools/call 等处理器
│   ├── SubscriptionFilter.h# 订阅过滤器（订阅什么方法/URI）
│   ├── SubscriptionRegistry.h # 订阅注册表：客户端维度聚合、事件通知
│   └── UriTemplate.h       # RFC 6570 URI Template 匹配
└── transport/              # 传输层
    ├── ITransport.h / IExecutor.h
    ├── StdioTransport.h    # stdin/stdout 行分隔 JSON-RPC
    └── StreamableHttpServer.h # 基于 net::http 的 HTTP + SSE 传输
```

## 整体架构

```
                    ┌────────────────────────────┐
                    │        McpServer           │
                    │  register_tool/resource/   │
                    │  prompt + listen_http /    │
                    │  run_stdio                 │
                    └──────┬───────────┬─────────┘
                           │           │
              ┌────────────▼──┐   ┌────▼──────────────────┐
              │ StdioTransport│   │ StreamableHttpServer  │
              │ (stdin/stdout)│   │ (net::http + SSE)     │
              └────────────┬──┘   └────┬──────────────────┘
                           │           │
              ┌────────────▼───────────▼─────────┐
              │  json_rpc::MethodDispatcher      │
              │  initialize / tools/list /       │
              │  tools/call / resources/... /    │
              │  subscriptions/listen ...        │
              └────────────┬─────────────────────┘
                           │
              ┌────────────▼─────────────────────┐
              │  业务线程池（Options.business_   │
              │  threads，队列上限防过载）        │
              │  执行 ToolExecutor 等业务逻辑     │
              └──────────────────────────────────┘
```

- **能力注册**：`register_*` 必须在 `listen_http` / `run_stdio` 之前完成，启动后注册表冻结。
- **输入校验**：工具入参按 `inputSchema`（JSON Schema）校验，校验通过才进业务 executor；业务函数只需处理语义。
- **错误码**：严格遵循 JSON-RPC 2.0（`-32700` ParseError、`-32601` MethodNotFound、`-32602` InvalidParams、`-32603` InternalError），并扩展 MCP 2026-07-28 的 `-32020` HeaderMismatch、`-32021` UnsupportedProtocolVersion。

## 快速上手

```cpp
#include "mcp/McpServer.h"

using json = nlohmann::json;

mcp::McpServer server({
    .name = "demo-server",
    .version = "1.0.0",
    .io_threads = 4,              // HTTP 传输的 IO 线程
    .business_threads = 4,        // 业务线程池（0 = 同步执行，仅测试用）
    .business_queue_limit = 4096, // 超出返回 -32603
});

// 1. 注册工具
server.register_tool(mcp::ToolDescriptor::make(
    "echo",
    "Echo the input string back.",
    json{{"type", "object"},
         {"properties", {{"text", {{"type", "string"}}}}},
         {"required", {"text"}}},
    [](const json &args, mcp::IRequestContext &) -> mcp::ToolResult {
        mcp::ToolResult r;
        r.content = json::array(
            {{{"type", "text"}, {"text", args["text"].get<std::string>()}}});
        return r;
    }));

// 2. 注册资源（reader 返回 MCP 规范格式的 json）
server.register_resource(mcp::ResourceDescriptor{
    .uri = "demo://greeting",
    .name = "Greeting",
    .description = "A fixed greeting resource",
    .mime_type = "text/plain",
    .reader = []() -> mcp::json {
        return {{"contents",
                 {{{"uri", "demo://greeting"},
                   {"mimeType", "text/plain"},
                   {"text", "Hello, MCP!"}}}}};
    },
});

// 3. 注册提示词（renderer 根据参数生成消息序列）
server.register_prompt(mcp::PromptDescriptor{
    .name = "greet",
    .description = "Generate a greeting prompt",
    .arguments = {{.name = "who", .description = "Who to greet",
                   .required = true}},
    .renderer = [](const mcp::json &args) -> mcp::json {
        return {{"messages",
                 {{{"role", "user"},
                   {"content",
                    {{"type", "text"},
                     {"text", "Hello " + args["who"].get<std::string>() +
                                  "!"}}}}}}}};
    },
});

// 4. 启动传输（二选一，互斥）
server.run_stdio();                          // 方式 A：stdio
// server.listen_http("0.0.0.0", 8080);      // 方式 B：HTTP + SSE
```

完整的端到端示例见 [examples/todos-server](../../examples/todos-server/)（基于 MySQL 的待办事项服务，含 tool / resource / prompt 三类注册）。

## 工具结果（ToolResult）

| 字段 | 说明 |
|---|---|
| `content` | 文本/图片等内容数组（MCP 规范格式） |
| `structured_content` | 结构化输出，与 `output_schema` 对应 |
| `is_error` | true 表示业务执行失败（作为错误结果返回给模型，而非协议错误） |

## 资源与订阅

- **资源**：`ResourceDescriptor` 通过 URI 标识；`ResourceTemplateDescriptor` 用 URI Template（RFC 6570，如 `demo://users/{id}`）匹配一类资源。
- **订阅**：客户端通过 `subscriptions/listen` 订阅资源变更；服务端资源更新后调用 `SubscriptionRegistry::notify(...)`，向所有匹配的订阅推送 `notifications/resources/updated`。订阅过滤器（`SubscriptionFilter`）经 JSON Schema 校验，服务端可协商缩减实际履行的范围（`honored_filter`）。

## 传输层细节

| 传输 | 特点 |
|---|---|
| `StdioTransport` | stdin/stdout 行分隔 JSON-RPC，适合本地进程间调用（Claude Desktop 等客户端） |
| `StreamableHttpServer` | 基于 `net::http::HttpServer`；单连接可承载多请求（按 `Mcp-Session-Id` 会话路由），支持 SSE 事件流推送；`sse_keepalive_interval` 配置心跳 |

## 配置项（Options）

| 项 | 默认 | 说明 |
|---|---|---|
| `name` / `version` | mcp-server / 1.0.0 | `initialize` 握手返回的服务信息 |
| `io_threads` | 4 | HTTP 传输 IO 线程数（stdio 不使用） |
| `business_threads` | 4 | 业务线程池大小；0 表示同步执行（仅测试） |
| `business_queue_limit` | 4096 | 业务队列上限，超出返回 -32603（过载保护） |
| `sse_keepalive_interval` | 30.0 s | SSE 心跳间隔，<= 0 禁用 |
| `supported_versions` | ["2026-07-28"] | 协议版本协商列表 |
| `discover_ttl_ms` | 3600000 | `server/discover` 结果缓存时长 |
| `instructions` | nullopt | 提供给 LLM 的自然语言使用指引 |

## 测试

单元测试位于 [tests/mcp](../../tests/mcp/)：json_rpc（类型、校验、分发、处理器）、server（tools / prompts / resources / subscriptions / uri_template）、transport 各自独立可测。

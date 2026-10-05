# McpServer

一个 Linux C++17 服务器项目，核心目标是从零实现三套高性能基础设施库，并基于它们构建一个遵循 [MCP（Model Context Protocol）](https://modelcontextprotocol.io) 规范的 JSON-RPC 服务器。

## 模块总览

| 模块 | 路径 | 说明 |
|---|---|---|
| logger | [src/logger/](src/logger/README.md) | 仿 spdlog 的高性能异步日志库，多 Sink、文件轮转、压缩/加密 |
| net | [src/net/](src/net/README.md) | 仿 muduo 的 Reactor 网络库（epoll + 非阻塞 I/O），含 HTTP 服务 |
| mcp | [src/mcp/](src/mcp/README.md) | MCP 协议 JSON-RPC 2.0 服务器，支持 stdio 与 Streamable HTTP 传输 |
| config | [src/config/](src/config/README.md) | 模板化的配置项系统，支持 JSON 文件加载 |
| db | [src/db/](src/db/README.md) | MySQL 数据库模块（libmysql），无锁连接池 + 预处理语句 |
| redis | [src/redis/](src/redis/README.md) | Redis 客户端（hiredis），同步/异步统一接口，支持连接池 |
| utils | [src/utils/](src/utils/README.md) | 基础工具：线程池、无锁队列、AES/Zlib 等 |

各模块的详细设计见对应 README。示例程序位于 [examples/](examples/)，命令行小工具位于 [tools/](tools/)。

## 整体架构

```
┌─────────────────────────────────────────────────────┐
│                    应用层 (examples)                  │
│        todos-server / logger example / ...           │
├─────────────────────────────────────────────────────┤
│  McpServer（门面）                                    │
│  ├── json_rpc   JSON-RPC 2.0 分发、校验、上下文        │
│  ├── server     tool / resource / prompt / 订阅注册    │
│  └── transport  StdioTransport / StreamableHttpServer │
├───────────────────────┬─────────────────────────────┤
│  net（Reactor 网络库） │  logger（异步日志）           │
│  EventLoop/Poller/    │  Logger/Sink/Rotater/        │
│  TcpServer/HttpServer │  AsyncLogger(线程池)          │
├───────────────────────┴─────────────────────────────┤
│  config（配置）  db（MySQL）  redis（hiredis）         │
├─────────────────────────────────────────────────────┤
│  utils：ThreadPool / MPMC 队列 / AES / Zlib / os      │
└─────────────────────────────────────────────────────┘
```

依赖关系：`mcp` 依赖 `net`（HTTP 传输）、`logger`、`utils`；`db`/`redis`/`logger` 依赖 `utils`；`config` 独立。

## 构建

### 环境要求

- Ubuntu 24.04（Linux，epoll/eventfd 平台）
- gcc 13.3.0+，C++17
- CMake >= 3.21
- [vcpkg](https://github.com/microsoft/vcpkg) 提供第三方依赖
- Google Test（测试）

### 第三方依赖

| 库 | 用途 |
|---|---|
| nlohmann/json | JSON 解析（config、mcp） |
| fmt | 格式化（logger、mcp） |
| GTest | 单元测试 |
| OpenSSL | 加解密（utils AES） |
| ZLIB | 压缩（utils、logger 轮转压缩） |
| libmysql | MySQL 客户端（db） |
| hiredis | Redis 客户端（redis） |

### 构建步骤

```bash
export VCPKG_ROOT=/path/to/vcpkg

# 使用 preset 配置（Debug，日常开发与测试）
cmake --preset default
cmake --build build -j$(nproc)

# 运行单元测试
cd build && ctest --output-on-failure
```

所有可执行文件输出到 `bin/` 目录。

### 基准测试（务必使用 Release 构建）

Debug 配置下 vcpkg 链接的是 Debug 版第三方库（如带断言的 `libfmtd.a`），实测同一份基准代码性能相差约 4 倍。性能数据必须以 Release 配置采集：

```bash
cmake --preset release
cmake --build build-release --target logger_bench dbpool_bench echo_bench queue_bench -j$(nproc)

./bin/tests/logger/logger_bench   # 日志吞吐：同步 vs 异步、NullSink vs 落盘
./bin/tests/db/dbpool_bench       # 连接池借还延迟与并发吞吐（Mock 连接）
./bin/tests/net/echo_bench        # TCP echo QPS（1 vs 4 IO 线程）
./bin/tests/utils/queue_bench     # 无锁队列 vs 阻塞队列吞吐
```

注意：虚拟机环境（`hypervisor` 标志 + 时钟/系统调用被拦截，单次 `clock_gettime` 实测 ~20µs）会让时间戳与网络类基准严重失真；纯内存基准（队列、连接池借还）与带时钟缓存的日志前端不受影响。日志与连接池的时间戳均已走 `utils::CachedClock` 后台缓存。

## 运行示例

```bash
# todos-server：基于 MySQL 的 MCP 待办事项服务（stdio 传输）
./bin/examples/todos-server/todos-server

# 日志库示例
./bin/examples/logger/example

# 日志查询工具
./bin/tools/log_tool --help
```

## 目录结构

```
.
├── CMakeLists.txt            # 顶层构建脚本
├── CMakePresets.json         # vcpkg + Debug 预设
├── src/                      # 核心库源码（7 个模块）
│   ├── config/  logger/  net/  mcp/
│   ├── db/  redis/  utils/
├── examples/                 # 示例程序（logger / utils / todos-server）
├── tests/                    # 单元测试（每个模块独立可测）
├── tools/                    # 命令行小工具（log_tool）
└── conf/                     # 配置文件样例
```

## 开发约定

- 代码风格：Google C++ Style Guide
- 设计流程：先设计接口 → 实现 → 编写测试
- 每个模块独立可测试（tests/ 下按模块组织）
- 优先使用标准库，仅在性能敏感处使用系统调用
- 不使用 spdlog / muduo 等第三方同类库，全部基于自建库实现

# MCP_Server 项目 Claude 辅助开发指令

## 项目概述
我们正在开发一个 Linux C++ 服务框架，包含三个核心模块与四个配套模块：
1. **logger**：仿 spdlog 的高性能日志库。
2. **net**：仿 muduo 的 Reactor 网络库（epoll + 非阻塞 I/O）。
3. **mcp**：实现 Anthropic MCP 协议的 JSON-RPC 服务器。
4. **config**：模板化配置项系统（JSON 加载）。
5. **db**：MySQL 数据库模块（无锁连接池）。
6. **redis**：Redis 客户端（同步/异步统一接口）。
7. **utils**：基础工具（线程池、无锁队列、AES/Zlib）。

各模块的设计与用法详见对应 README：根目录 [README.md](../README.md)、[src/logger/README.md](../src/logger/README.md)、[src/net/README.md](../src/net/README.md)、[src/mcp/README.md](../src/mcp/README.md)、[src/config/README.md](../src/config/README.md)、[src/db/README.md](../src/db/README.md)、[src/redis/README.md](../src/redis/README.md)、[src/utils/README.md](../src/utils/README.md)。修改模块前先阅读对应 README。

## 开发环境
- 操作系统：Ubuntu 24.04
- 编译器：gcc version 13.3.0，支持 C++17
- 构建系统：CMake >= 3.21（使用 CMakePresets 配置）
- 测试框架：Google Test (gtest)
- 包管理：通过 vcpkg 引入第三方库（nlohmann/json, gtest, fmt, OpenSSL, ZLIB, libmysql, hiredis）
- 代码风格：Google C++ Style Guide

## 代码结构
- src/config/... : 配置模块
- src/logger/... : 日志模块
- src/net/... : 网络模块
- src/mcp/... : MCP 协议模块
- src/db/... : MySQL 数据库模块
- src/redis/... : Redis 客户端模块
- src/utils/... : 基础工具库
- examples/ : 示例程序（logger / utils / todos-server）
- tools/ : 命令行小工具（log_tool）
- tests/ : 测试代码
- 各模块通过 CMake 子目录构建为静态库（csj_utils、csj_config、csj_log、csj_net、csj_mcp、csj_db、csj_redis），可执行文件输出到 bin/ 目录

## 模块依赖关系
- utils：独立，被其余模块共享
- config：独立
- logger：依赖 utils
- net：依赖 utils、logger
- db：依赖 utils、config
- redis：依赖 net、logger
- mcp：依赖 net、logger、utils

## 核心设计约束
- Logger：多 Sink、多 Formatter，类似 spdlog 的 API 风格，但不使用 spdlog 代码。异步日志器借助 utils::ThreadPool 后台落盘，日志事件打包进 LogEventBuffer 提交；支持按大小/按时间文件轮转与压缩/加密处理链。
- Net：每个线程一个 EventLoop，epoll 驱动，非阻塞。TcpServer 使用 Round-Robin 分配连接到 EventLoop 线程池。跨线程操作通过 runInLoop/queueInLoop。
- MCP：严格实现 JSON-RPC 2.0，支持 tools/list、tools/call 等标准方法，错误码遵循协议规范（-32700/-32601/-32602/-32603 及 MCP 扩展码）。能力注册后启动 stdio 或 Streamable HTTP 传输。
- DB：借/还完全无锁的连接池（mpmc_bounded_queue 空闲队列 + 维护线程），外部输入必须走 IStmt 预处理防注入；连接借出后请勿跨线程传递。
- Redis：IRedis 统一连接接口，同步（阻塞式）与异步（集成 net EventLoop）两种客户端，接口为连接池管理预留。
- Utils：性能敏感处使用无锁结构（MPMC 队列、SPSC 环形队列）。

## 开发流程要求
- 先设计接口，再实现，最后写测试。
- 每个模块独立可测试。
- 当你生成代码时，请添加 Doxygen 风格注释和关键逻辑解释。
- 优先使用标准库，仅在性能敏感处使用系统调用。
- 如果遇到编译错误，请直接读取错误信息并自主修复，最多尝试 3 次。

## Claude 行为准则
- 每次只专注于我请求的一个模块或功能。
- 生成文件时，先列出将要创建/修改的文件清单，经我确认后一次性写入。
- 解释复杂设计时，附带简短的架构说明或图表（ASCII art）。
- 不要假设未明确声明的第三方库（如不要直接用 spdlog 或 muduo），必须按我们的自建库实现。
- 修改模块行为或新增接口后，同步更新对应模块的 README.md。

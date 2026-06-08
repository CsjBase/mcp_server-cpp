# MCP_Server 项目 Claude 辅助开发指令

## 项目概述
我们正在开发一个 Linux C++ 服务器，包含三个模块：
1. **logger**：仿 spdlog 的高性能日志库。
2. **net**：仿 muduo 的 Reactor 网络库（epoll + 非阻塞 I/O）。
3. **mcp**：实现 Anthropic MCP 协议的 JSON-RPC 服务器。

## 开发环境
- 操作系统：Ubuntu 24.04
- 编译器：gcc version 13.3.0，支持 C++17
- 构建系统：CMake >= 3.16
- 测试框架：Google Test (gtest)
- 包管理：通过 vcpkg 引入第三方库（nlohmann/json, gtest,fmtlib 等）
- 代码风格：Google C++ Style Guide，命名空间 logger / net / mcp

## 代码结构
- src/logger/... : 日志模块
- src/net/... : 网络模块
- src/mcp/... : MCP 协议模块
- tests/ : gtest 单元测试
- 所有模块通过 CMake 子目录构建，最终链接为 mcp_server 可执行文件

## 核心设计约束
- Logger：异步环形缓冲区，多 Sink，类似 spdlog 的 API 风格，但不使用 spdlog 代码。
- Net：每个线程一个 EventLoop，epoll 驱动，非阻塞。TcpServer 使用 Round-Robin 分配连接到 EventLoop 线程池。
- MCP：严格实现 JSON-RPC 2.0，支持 tools/list、tools/call 等标准方法，错误码遵循协议规范。

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
#include "mcp/transport/StdioTransport.h"
#include "mcp/base/Log.h"

#include <iostream>
#include <string>

namespace mcp
{

    StdioTransport::~StdioTransport()
    {
        stop();
    }

    void StdioTransport::set_handler(MessageHandler handler)
    {
        handler_ = std::move(handler);
    }

    void StdioTransport::start()
    {
        running_ = true;
        MCP_LOG_INFO("stdio transport started");

        std::string line;
        while (running_ && read_line(line))
        {
            if (line.empty())
                continue;

            // 将原始消息交给 MethodDispatcher
            // handler_ 返回 std::optional<std::string>：
            //   有值 → 是请求，需要响应
            //   无值 → 是通知，不需要响应
            std::optional<std::string> response;
            try
            {
                response = handler_(line);
            }
            catch (const std::exception &e)
            {
                // handler_ 自身不应抛异常（MethodDispatcher 已捕获），
                // 这里是最后一道防线
                MCP_LOG_ERROR("handler threw exception: {}", e.what());
                continue;
            }

            if (response)
            {
                send(*response);
            }
        }

        running_ = false;
        eof_ = true;
        MCP_LOG_INFO("stdio transport stopped (EOF)");
    }

    void StdioTransport::stop()
    {
        running_ = false;
        // 关闭 stdin 的读取由客户端负责（关闭子进程 stdin）
        // 服务端本身无法强制中断阻塞的 std::getline
    }

    void StdioTransport::send(const std::string &raw)
    {
        // 关键约束 1：写入 stdout 的消息不得包含嵌入换行符
        // JSON-RPC 消息本身是单行 JSON，但防御性地校验
        if (raw.find('\n') != std::string::npos)
        {
            MCP_LOG_ERROR("refusing to write message with embedded newline");
            return;
        }

        // 关键约束 2：多线程写入必须串行化
        // 进度通知可能从 worker 线程发出，与主线程的响应交错
        std::lock_guard<std::mutex> lock(write_mutex_);

        std::cout << raw << '\n';
        // 关键约束 3：必须 flush
        // 未 flush 会导致客户端因等待响应而挂起
        std::cout.flush();
    }

    bool StdioTransport::read_line(std::string &out)
    {
        if (!std::getline(std::cin, out))
        {
            return false; // EOF 或读取错误
        }
        // 去除可能的 Windows \r
        if (!out.empty() && out.back() == '\r')
        {
            out.pop_back();
        }
        return true;
    }

} // namespace mcp
#pragma once

#include "logger/LoggerManager.h"
#include "logger/SynchronousFactory.h"
#include "logger/log.h"

#define MCP_LOGGER() mcp::Log::instance().getLogger()

#define MCP_LOG_TRACE(...) LOG_TRACE(MCP_LOGGER(), ##__VA_ARGS__)
#define MCP_LOG_DEBUG(...) LOG_DEBUG(MCP_LOGGER(), ##__VA_ARGS__)
#define MCP_LOG_INFO(...) LOG_INFO(MCP_LOGGER(), ##__VA_ARGS__)
#define MCP_LOG_WARN(...) LOG_WARN(MCP_LOGGER(), ##__VA_ARGS__)
#define MCP_LOG_ERROR(...) LOG_ERROR(MCP_LOGGER(), ##__VA_ARGS__)
#define MCP_LOG_FATAL(...) LOG_FATAL(MCP_LOGGER(), ##__VA_ARGS__)

namespace mcp
{
    class Log
    {
    public:
        static Log &instance();
        logger::Logger::ptr getLogger();

    private:
        Log();
        Log(const Log &) = delete;
        Log &operator=(const Log &) = delete;
    };

}
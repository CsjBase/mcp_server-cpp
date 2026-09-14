#pragma once

#include "logger/LoggerManager.h"
#include "logger/SynchronousFactory.h"
#include "logger/log.h"

// #define SRC_LOCATION \
//     logger::details::SourceLocation { __FILE_NAME__, __LINE__, __FUNCTION__ }

// #define LOG_LEVEL(level, ...) \
//     net::Log::instance().getLogger()->log(SRC_LOCATION, level, ##__VA_ARGS__)

#define NET_LOGGER() net::Log::instance().getLogger()

#define NET_LOG_TRACE(...) LOG_TRACE(NET_LOGGER(), ##__VA_ARGS__)
#define NET_LOG_DEBUG(...) LOG_DEBUG(NET_LOGGER(), ##__VA_ARGS__)
#define NET_LOG_INFO(...) LOG_INFO(NET_LOGGER(), ##__VA_ARGS__)
#define NET_LOG_WARN(...) LOG_WARN(NET_LOGGER(), ##__VA_ARGS__)
#define NET_LOG_ERROR(...) LOG_ERROR(NET_LOGGER(), ##__VA_ARGS__)
#define NET_LOG_FATAL(...) LOG_FATAL(NET_LOGGER(), ##__VA_ARGS__)

namespace net
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
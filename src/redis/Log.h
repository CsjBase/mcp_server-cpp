#pragma once

#include "logger/LoggerManager.h"
#include "logger/SynchronousFactory.h"
#include "logger/log.h"

#define REDIS_LOGGER() redis::Log::instance().getLogger()

#define REDIS_LOG_TRACE(...) LOG_TRACE(REDIS_LOGGER(), ##__VA_ARGS__)
#define REDIS_LOG_DEBUG(...) LOG_DEBUG(REDIS_LOGGER(), ##__VA_ARGS__)
#define REDIS_LOG_INFO(...) LOG_INFO(REDIS_LOGGER(), ##__VA_ARGS__)
#define REDIS_LOG_WARN(...) LOG_WARN(REDIS_LOGGER(), ##__VA_ARGS__)
#define REDIS_LOG_ERROR(...) LOG_ERROR(REDIS_LOGGER(), ##__VA_ARGS__)
#define REDIS_LOG_FATAL(...) LOG_FATAL(REDIS_LOGGER(), ##__VA_ARGS__)

namespace redis
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

} // namespace redis

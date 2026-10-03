#pragma once

#include "logger/LoggerManager.h"
#include "logger/SynchronousFactory.h"
#include "logger/log.h"

// #define SRC_LOCATION \
//     logger::details::SourceLocation { __FILE_NAME__, __LINE__, __FUNCTION__ }

// #define LOG_LEVEL(level, ...) \
//     net::Log::instance().getLogger()->log(SRC_LOCATION, level, ##__VA_ARGS__)

#define DB_LOGGER() db::Log::instance().getLogger()

#define DB_LOG_TRACE(...) LOG_TRACE(DB_LOGGER(), ##__VA_ARGS__)
#define DB_LOG_DEBUG(...) LOG_DEBUG(DB_LOGGER(), ##__VA_ARGS__)
#define DB_LOG_INFO(...) LOG_INFO(DB_LOGGER(), ##__VA_ARGS__)
#define DB_LOG_WARN(...) LOG_WARN(DB_LOGGER(), ##__VA_ARGS__)
#define DB_LOG_ERROR(...) LOG_ERROR(DB_LOGGER(), ##__VA_ARGS__)
#define DB_LOG_FATAL(...) LOG_FATAL(DB_LOGGER(), ##__VA_ARGS__)

namespace db
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
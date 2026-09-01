#pragma once

#include "logger/LoggerManager.h"
#include "logger/SynchronousFactory.h"

#define SRC_LOCATION \
    logger::details::SourceLocation { __FILE_NAME__, __LINE__, __FUNCTION__ }

#define LOG_LEVEL(level, ...) \
    net::Log::instance().getLogger()->log(SRC_LOCATION, level, ##__VA_ARGS__)

#define LOG_TRACE(...) LOG_LEVEL(logger::LogLevel::Trace, ##__VA_ARGS__)
#define LOG_DEBUG(...) LOG_LEVEL(logger::LogLevel::Debug, ##__VA_ARGS__)
#define LOG_INFO(...) LOG_LEVEL(logger::LogLevel::Info, ##__VA_ARGS__)
#define LOG_WARN(...) LOG_LEVEL(logger::LogLevel::Warn, ##__VA_ARGS__)
#define LOG_ERROR(...) LOG_LEVEL(logger::LogLevel::Error, ##__VA_ARGS__)
#define LOG_FATAL(...) LOG_LEVEL(logger::LogLevel::Fatal, ##__VA_ARGS__)

namespace net
{
    class Log
    {
    public:
        static Log &instance();
        logger::Logger::ptr getLogger()
        {
            return m_logger;
        }

    private:
        Log();
        Log(const Log &) = delete;
        Log &operator=(const Log &) = delete;

    private:
        logger::Logger::ptr m_logger;
    };

}
#pragma once

#include "logger/LoggerManager.h"
#include "logger/SynchronousFactory.h"

#define LOG_TRACE(Logger, ...) LOG_LEVEL(Logger, logger::LogLevel::Trace, ##__VA_ARGS__)
#define LOG_DEBUG(Logger, ...) LOG_LEVEL(Logger, logger::LogLevel::Debug, ##__VA_ARGS__)
#define LOG_INFO(Logger, ...) LOG_LEVEL(Logger, logger::LogLevel::Info, ##__VA_ARGS__)
#define LOG_WARN(Logger, ...) LOG_LEVEL(Logger, logger::LogLevel::Warn, ##__VA_ARGS__)
#define LOG_ERROR(Logger, ...) LOG_LEVEL(Logger, logger::LogLevel::Error, ##__VA_ARGS__)
#define LOG_FATAL(Logger, ...) LOG_LEVEL(Logger, logger::LogLevel::Fatal, ##__VA_ARGS__)

#define SRC_LOCATION \
    logger::details::SourceLocation { __FILE_NAME__, __LINE__, __FUNCTION__ }

#define LOG_LEVEL(logger, level, ...) \
    logger->log(SRC_LOCATION, level, ##__VA_ARGS__)

#define LOGGER_DEFAULT() logger::LoggerManager::instance().get_default_logger()
#define LOGGER_NAME(name) logger::LoggerManager::instance().get_logger(name)

namespace logger
{
    using DefaultFactory = SynchronousFactory;

}
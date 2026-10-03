#include "Log.h"

#include "logger/sinks/RotatingFileLogSink.h"
#include "logger/sinks/ColorLogSink.h"
#include "logger/AsynchronousFactory.h"

#include "logger/LogConfig.h"

namespace db
{

    Log &Log::instance()
    {
        static Log log;
        return log;
    }
    logger::Logger::ptr Log::getLogger()
    {
        return logger::LoggerManager::instance().get_logger("db");
    }

    Log::Log()
    {
        auto logger = logger::LoggerManager::instance().get_logger("db");
        if (!logger)
        {
            // auto logger = logger::create_rotating_file_mt_logger("db", "logs/db.log", 1024 * 1024 * 5, 3);
            // logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%^%l%$] [%s:%#] %v");
            // logger->set_level(logger::LogLevel::Trace);

            auto rotating_file_mt_sink = std::make_shared<logger::RotatingFileLogSinkMT>("logs/db.log", 1024 * 1024 * 5, 3);
            auto color_sink = std::make_shared<logger::StderrColorLogSinkMT>();
            logger = logger::create_logger("db", {rotating_file_mt_sink, color_sink});
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%^%l%$] [%s:%#] %v");
            logger->set_level(logger::LogLevel::Debug);
        }
    }
}
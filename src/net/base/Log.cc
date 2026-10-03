#include "net/base/Log.h"

#include "logger/sinks/RotatingFileLogSink.h"
#include "logger/sinks/ColorLogSink.h"
#include "logger/AsynchronousFactory.h"

#include "logger/LogConfig.h"

namespace net
{

    Log &Log::instance()
    {
        static Log log;
        return log;
    }
    logger::Logger::ptr Log::getLogger()
    {
        return logger::LoggerManager::instance().get_logger("net");
    }

    Log::Log()
    {
        auto logger = logger::LoggerManager::instance().get_logger("net");
        if (!logger)
        {
            auto rotating_file_mt_sink = std::make_shared<logger::RotatingFileLogSinkMT>("logs/net.log", 1024 * 1024 * 5, 3);
            auto color_sink = std::make_shared<logger::StderrColorLogSinkMT>();
            logger = logger::create_async_logger("net", {rotating_file_mt_sink, color_sink});
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%^%l%$] [%s:%#] %v");
            logger->set_level(logger::LogLevel::Trace);
        }
    }
}
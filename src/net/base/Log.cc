#include "net/base/Log.h"

#include "logger/sinks/RotatingFileLogSink.h"
#include "logger/sinks/ColorLogSink.h"
#include "logger/AsynchronousFactory.h"

namespace net
{

    Log &Log::instance()
    {
        static Log log;
        return log;
    }
    Log::Log()
    {
        auto rotating_file_mt_sink = std::make_shared<logger::RotatingFileLogSinkMT>("logs/net.log", 1024 * 1024 * 5, 3);
        auto color_sink = std::make_shared<logger::StdoutColorLogSinkMT>();
        m_logger = logger::create_async_logger("net", {rotating_file_mt_sink, color_sink});
        m_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%^%l%$] [%s:%#] %v");
        m_logger->set_level(logger::LogLevel::Trace);
    }
}
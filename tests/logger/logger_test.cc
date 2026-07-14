#include "logger/Logger.h"
#include "logger/sinks/BasicFileLogSink.h"
#include "logger/sinks/ColorLogSink.h"

using namespace logger;

#define SRC_LOCATION \
    details::SourceLocation { __FILE_NAME__, __LINE__, __FUNCTION__ }

int main()
{
    Logger logger("test", std::make_shared<StdoutColorLogSinkST>());
    logger.set_level(LogLevel::Debug);
    logger.debug("this is a debug message");
    logger.info("this is an info message");
    logger.warn("this is a warning message");
    logger.error("this is an error message");
    logger.fatal("this is a fatal message");
    logger.trace("this is a trace message");
    logger.flush();

    for (int i = 0; i < 10; i++)
    {
        logger.debug("this is a debug message {}", i);
    }

    for (int i = 0; i < 10; i++)
    {
        logger.log(SRC_LOCATION, LogLevel::Debug, "this is a debug message {}", i);
    }
}
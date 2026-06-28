#include "logger/Logger.h"
#include "logger/sinks/ColorLogSink.h"

using namespace logger;

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

    logger.set_level(LogLevel::Off);
    logger.debug("this message should not be logged");
    logger.info("this message should not be logged");
    logger.warn("this message should not be logged");
    logger.error("this message should not be logged");
    logger.fatal("this message should not be logged");
    logger.trace("this message should not be logged");
    logger.flush();
}
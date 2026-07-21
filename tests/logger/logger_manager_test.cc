#include "logger/LoggerManager.h"
#include "logger/sinks/ColorLogSink.h"
#include "logger/sinks/BasicFileLogSink.h"

using namespace logger;

int main()
{
    auto &manager = LoggerManager::instance();
    manager.get_default_logger()->debug("This is a debug message");
    manager.get_default_logger()->info("This is an info message");
    manager.get_default_logger()->warn("This is a warning message");
    manager.get_default_logger()->error("This is an error message");
    manager.get_default_logger()->fatal("This is a fatal message");

    manager.add_logger(std::make_shared<Logger>("test", std::make_shared<BasicFileLogSinkMT>("../../../logs/test_logger_manager.log")));

    auto logger = manager.get_logger("test");
    logger->debug("This is a debug message");
    logger->info("This is an info message");
    logger->warn("This is a warning message");
    logger->error("This is an error message");
    logger->fatal("This is a fatal message");
}
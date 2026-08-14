#include "logger/AsyncLogger.h"
#include "logger/sinks/BasicFileLogSink.h"
#include "utils/thread_pool.h"

#include "utils/os.h"

using namespace logger;

#define SRC_LOCATION \
    details::SourceLocation { __FILE_NAME__, __LINE__, __FUNCTION__ }

void test_async_logger()
{
    auto tp = std::make_shared<utils::ThreadPool>(1, 1024);
    auto async_logger = std::make_shared<AsyncLogger>("test", std::make_shared<BasicFileLogSinkMT>("../../../logs/test_async_logger.log"), tp);
    async_logger->set_level(LogLevel::Debug);
    async_logger->debug("this is a debug message");
    async_logger->info("this is an info message");
    async_logger->warn("this is a warning message");
    async_logger->error("this is an error message");

    utils::sleep_for_millis(500);
    for (int i = 0; i < 10; i++)
    {
        async_logger->debug("this is a debug message {}", i);
    }

    utils::sleep_for_millis(500);
    for (int i = 0; i < 10; i++)
    {
        async_logger->log(SRC_LOCATION, LogLevel::Debug, "this is a debug message {}", i);
    }
}

int main()
{
    test_async_logger();
}
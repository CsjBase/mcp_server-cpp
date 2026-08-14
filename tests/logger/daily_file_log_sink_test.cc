#include "logger/sinks/DailyFileLogSink.h"
#include "logger/rotater/RotatedFileHandler.h"
#include "logger/rotater/CompressHandler.h"
#include "logger/rotater/EncryptHandler.h"
#include "utils/aes_crypt.h"
#include "utils/zlib_compress.h"

#include <chrono>
#include <thread>

using namespace logger;
using namespace utils;

DailyFileLogSinkMT sink_mt("/home/csj/code/mcp_server/logs/test_daily_file_sink_mt.log", 0, 0, true, 5);

void test_MT()
{
    auto now = std::chrono::system_clock::now();
    for (int day = 0; day < 7; ++day)
    {
        auto t = now + std::chrono::hours(24 * day);
        std::string msg = "test daily file Multi-Thread sink msg ";
        details::LogEvent event(t,
                                details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__},
                                "test", LogLevel::Trace, msg);
        sink_mt.log(event);
    }
}

void test_ST()
{
    auto now = std::chrono::system_clock::now();
    DailyFileLogSinkST sink("/home/csj/code/mcp_server/logs/test_daily_file_sink_st.log", 0, 0, true, 5);

    for (int day = 0; day < 7; ++day)
    {
        auto t = now + std::chrono::hours(24 * day);
        std::string msg = "test daily file Single-Thread sink msg ";
        details::LogEvent event(t,
                                details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__},
                                "test", LogLevel::Trace, msg);
        sink.log(event);
    }
}
std::unique_ptr<CompositeHandler> handler = std::make_unique<CompositeHandler>();
std::unique_ptr<DailyFileLogSinkMT> sink;

void test_ST_handler()
{
    auto now = std::chrono::system_clock::now();
    for (int day = 0; day < 7; ++day)
    {
        auto t = now + std::chrono::hours(24 * day);
        std::string msg = "test daily file handler Single-Thread sink msg ";
        details::LogEvent event(t,
                                details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__},
                                "test", LogLevel::Trace, msg);
        sink->log(event);
    }
}

// void test_ST_handler()
// {
//     auto now = std::chrono::system_clock::now();
//     std::unique_ptr<CompositeHandler> handler = std::make_unique<CompositeHandler>();
//     handler->add(std::make_unique<CompressHandler>(std::make_unique<ZlibCompress>()));
//     handler->add(std::make_unique<EncryptHandler>(std::make_unique<AesCrypt>("test_key")));
//     DailyFileLogSinkST sink("/home/csj/code/mcp_server/logs/test_daily_file_sink_st_handler.log", 0, 0, true, 5, std::move(handler));

//     for (int day = 0; day < 7; ++day)
//     {
//         auto t = now + std::chrono::hours(24 * day);
//         std::string msg = "test daily file Single-Thread sink msg ";
//         details::LogEvent event(t,
//                                 details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__},
//                                 "test", LogLevel::Trace, msg);
//         sink.log(event);
//     }
// }

int main(int argc, char **argv)
{
    // test_ST();

    handler->add(std::make_unique<CompressHandler>(std::make_unique<ZlibCompress>()));
    handler->add(std::make_unique<EncryptHandler>(std::make_unique<AesCrypt>("test_key")));
    sink = std::make_unique<DailyFileLogSinkMT>("/home/csj/code/mcp_server/logs/test_daily_handler_file_sink_st.log", 0, 0, true, 5, std::move(handler));
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%l] [%s:%#] %v";
    sink->set_pattern(pattern);

    test_ST_handler();

    // std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%l] [%s:%#] %v";
    // sink_mt.set_pattern(pattern);

    // std::thread t1(test_MT);
    // std::thread t2(test_MT);
    // std::thread t3(test_MT);
    // std::thread t4(test_MT);

    // t1.join();
    // t2.join();
    // t3.join();
    // t4.join();

    return 0;
}

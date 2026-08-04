#include "logger/sinks/RotatingFileLogSink.h"
#include "logger/handlers/RotatedFileHandler.h"
#include "logger/handlers/CompressHandler.h"
#include "logger/handlers/EncryptHandler.h"
#include "utils/aes_crypt.h"
#include "utils/zlib_compress.h"

#include <chrono>
#include <thread>

using namespace logger;
using namespace utils;

RotatingFileLogSinkMT sink_mt("../../../logs/test_rotating_file_sink_mt.log", 1024 * 1024, 10);

void test_MT()
{
    for (int i = 0; i < 10000; ++i)
    {
        std::string msg = "test stdout Multi-Thread sink msg " + std::to_string(i);
        details::LogEvent event(std::chrono::system_clock::now(), details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__}, "test", LogLevel::Trace, msg);
        sink_mt.log(event);
    }
}
void test_ST()
{

    RotatingFileLogSinkST sink("../../../logs/test_rotating_file_sink_st.log", 1024 * 1024, 10);
    for (int i = 0; i < 10000; ++i)
    {
        std::string msg = "test stdout Single-Thread sink msg " + std::to_string(i);
        details::LogEvent event(std::chrono::system_clock::now(), details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__}, "test", LogLevel::Trace, msg);
        sink.log(event);
    }
}

void test_ST_handler()
{
    auto now = std::chrono::system_clock::now();
    std::unique_ptr<CompositeHandler> handler = std::make_unique<CompositeHandler>();
    handler->add(std::make_unique<CompressHandler>(std::make_unique<ZlibCompress>()));
    handler->add(std::make_unique<EncryptHandler>(std::make_unique<AesCrypt>("test_key")));
    RotatingFileLogSinkST sink("../../../logs/test_rotating_file_sink_st_handler.log", 1024 * 1024, 10, false, std::move(handler));

    for (int i = 0; i < 10000; ++i)
    {
        std::string msg = "test stdout Single-Thread sink msg " + std::to_string(i);
        details::LogEvent event(std::chrono::system_clock::now(), details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__}, "test", LogLevel::Trace, msg);
        sink.log(event);
    }
}

int main(int argc, char **argv)
{
    test_ST();
    test_ST_handler();

    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%l] [%s:%#] %v";
    sink_mt.set_pattern(pattern);

    std::thread t1(test_MT);
    std::thread t2(test_MT);
    std::thread t3(test_MT);
    std::thread t4(test_MT);

    t1.join();
    t2.join();
    t3.join();
    t4.join();

    return 0;
}
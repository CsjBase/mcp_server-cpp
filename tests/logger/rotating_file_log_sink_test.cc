#include "logger/sinks/RotatingFileLogSink.h"
#include "logger/rotater/RotatedFileHandler.h"
#include "logger/rotater/CompressHandler.h"
#include "logger/rotater/EncryptHandler.h"
#include "utils/aes_crypt.h"
#include "utils/zlib_compress.h"
#include "logger/rotater/RotationStrategy.h"

#include <openssl/crypto.h>
#include <chrono>
#include <thread>

using namespace logger;
using namespace utils;

RotatingFileLogSinkMT sink_mt("/home/csj/code/mcp_server/logs/test_rotating_file_sink_mt.log", 1024 * 1024, 10);

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

    RotatingFileLogSinkST sink("/home/csj/code/mcp_server/logs/test_rotating_file_sink_st.log", 1024 * 1024, 10);
    for (int i = 0; i < 10000; ++i)
    {
        std::string msg = "test stdout Single-Thread sink msg " + std::to_string(i);
        details::LogEvent event(std::chrono::system_clock::now(), details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__}, "test", LogLevel::Trace, msg);
        sink.log(event);
    }
}

std::unique_ptr<CompositeHandler> handler = std::make_unique<CompositeHandler>();
std::unique_ptr<RotatingFileLogSinkMT> sink;

void test_MT_handler()
{
    for (int i = 0; i < 100000; ++i)
    {
        std::string msg = "test rotating_file_sink_mt_handler Multi-Thread sink msg " + std::to_string(i);
        details::LogEvent event(std::chrono::system_clock::now(), details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__}, "test", LogLevel::Trace, msg);
        sink->log(event);
    }
}

int main(int argc, char **argv)
{
    OPENSSL_init_crypto(OPENSSL_INIT_NO_ATEXIT, NULL);

    handler->add(std::make_unique<CompressHandler>(std::make_unique<ZlibCompress>()));
    handler->add(std::make_unique<EncryptHandler>(std::make_unique<AesCrypt>("test_key")));
    sink = std::make_unique<RotatingFileLogSinkMT>("/home/csj/code/mcp_server/logs/test_rotating_file_sink_mt_handler.log", 1024 * 1024, 5, false, std::move(handler));

    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%l] [%s:%#] %v";
    sink->set_pattern(pattern);

    const int N = 5;
    std::vector<std::thread> threads(N);
    for (int i = 0; i < N; ++i)
    {
        threads[i] = std::thread(test_MT_handler);
    }
    for (int i = 0; i < N; ++i)
    {
        threads[i].join();
    }

    // test_ST();
    // test_ST_handler();

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
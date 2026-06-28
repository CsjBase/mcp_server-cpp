#include "logger/sinks/BasicFileLogSink.h"

#include <chrono>
#include <thread>

using namespace logger;

BasicFileLogSinkMT sink_mt("../../../logs/test_basic_file_sink_mt.log", true);

void test_MT()
{
    for (int i = 0; i < 100; ++i)
    {
        std::string msg = "test stdout Multi-Thread sink msg " + std::to_string(i);
        details::LogEvent event(std::chrono::system_clock::now(), details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__}, "test", LogLevel::Trace, msg);
        sink_mt.log(event);
    }
}
void test_ST()
{

    BasicFileLogSinkST sink("../../../logs/test_basic_file_sink_st.log", true);
    for (int i = 0; i < 100; ++i)
    {
        std::string msg = "test stdout Single-Thread sink msg " + std::to_string(i);
        details::LogEvent event(std::chrono::system_clock::now(), details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__}, "test", LogLevel::Trace, msg);
        sink.log(event);
    }
}

int main(int argc, char **argv)
{
    test_ST();

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
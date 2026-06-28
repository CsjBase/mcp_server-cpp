#include "logger/sinks/StdoutLogSink.h"

#include <chrono>
#include <thread>

using namespace logger;

void test_MT()
{
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%l] [%s:%#] %v";
    StdoutLogSinkMT sink;
    sink.set_pattern(pattern);
    for (int i = 0; i < 100; ++i)
    {
        std::string msg = "test stdout Multi-Thread sink msg " + std::to_string(i);
        details::LogEvent event(std::chrono::system_clock::now(), details::SourceLocation{__FILE_NAME__, __LINE__, __FUNCTION__}, "test", LogLevel::Trace, msg);
        sink.log(event);
    }
}
void test_ST()
{
    StdoutLogSinkST sink;
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
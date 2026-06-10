#include "logger/sinks/ColorLogSink.h"

#include <chrono>
#include <thread>

using namespace logger;

void test_MT()
{
    std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%t] [%n] [%^%l%$] [%s:%#] %v";
    StdoutColorLogSinkMT sink;
    sink.set_pattern(pattern);
    for (int i = 0; i < 10; ++i)
    {
        std::string msg = "test stdout Multi-Thread sink msg " + std::to_string(i);
        details::LogEvent event1(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Trace, msg);
        details::LogEvent event2(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Debug, msg);
        details::LogEvent event3(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Info, msg);
        details::LogEvent event4(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Warn, msg);
        details::LogEvent event5(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Error, msg);
        details::LogEvent event6(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Fatal, msg);
        sink.log(event1);
        sink.log(event2);
        sink.log(event3);
        sink.log(event4);
        sink.log(event5);
        sink.log(event6);
    }
}
void test_ST()
{
    StdoutColorLogSinkST sink;
    for (int i = 0; i < 10; ++i)
    {
        std::string msg = "test stdout Single-Thread sink msg " + std::to_string(i);
        details::LogEvent event1(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Trace, msg);
        details::LogEvent event2(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Debug, msg);
        details::LogEvent event3(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Info, msg);
        details::LogEvent event4(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Warn, msg);
        details::LogEvent event5(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Error, msg);
        details::LogEvent event6(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Fatal, msg);
        sink.log(event1);
        sink.log(event2);
        sink.log(event3);
        sink.log(event4);
        sink.log(event5);
        sink.log(event6);
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
#include "logger/sinks/DailyFileLogSink.h"

#include <chrono>
#include <thread>

using namespace logger;

DailyFileLogSinkMT sink_mt("../../../logs/test_daily_file_sink_mt.log", 0, 0, false, 5);

void test_MT()
{
    auto time = std::chrono::system_clock::now();
    for (int i = 0; i < 10000; ++i)
    {
        std::string msg = "test stdout Multi-Thread sink msg " + std::to_string(i);
        details::LogEvent event(time, __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Trace, msg);
        sink_mt.log(event);
        time += std::chrono::minutes(1);
    }
}
void test_ST()
{

    auto time = std::chrono::system_clock::now();
    DailyFileLogSinkST sink("../../../logs/test_daily_file_sink_st.log", 0, 0, false, 5);
    for (int i = 0; i < 10000; ++i)
    {
        std::string msg = "test stdout Single-Thread sink msg " + std::to_string(i);
        details::LogEvent event(time, __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Trace, msg);
        sink.log(event);
        time += std::chrono::minutes(1);
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
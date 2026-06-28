#include "logger/sinks/RotatingFileLogSink.h"
#include "logger/sinks/DailyFileLogSink.h"

#include <iostream>

void testRotatingFile_calc_filename()
{
    std::string filename = "log.txt";
    for (int i = 0; i < 100; i++)
    {
        std::cout << logger::RotatingFileLogSink<utils::null_mutex>::calc_filename(filename, i) << std::endl;
    }
}

void DailyFile_calc_filename()
{
    auto now = std::chrono::system_clock::now();
    std::cout << logger::DailyFileLogSink<utils::null_mutex>::calc_filename(".log", logger::DailyFileLogSink<utils::null_mutex>::now_tm(now)) << std::endl;
    std::cout << logger::DailyFileLogSink<utils::null_mutex>::calc_filename("a.log", logger::DailyFileLogSink<utils::null_mutex>::now_tm(now)) << std::endl;
    std::cout << logger::DailyFileLogSink<utils::null_mutex>::calc_filename("/a/.log", logger::DailyFileLogSink<utils::null_mutex>::now_tm(now)) << std::endl;
}

void testCircularQueue()
{
    utils::circular_q<int> q(5);
}

int main()
{
    // testRotatingFile_calc_filename();
    DailyFile_calc_filename();

    return 0;
}
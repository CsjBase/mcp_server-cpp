#include "logger/sinks/RotatingFileLogSink.h"

#include <iostream>
int main()
{
    std::string filename = "log.txt";
    for (int i = 0; i < 100; i++)
    {
        std::cout << logger::RotatingFileLogSink<utils::null_mutex>::calc_filename(filename, i) << std::endl;
    }
    return 0;
}
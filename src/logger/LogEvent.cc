#include "logger/LogEvent.h"
#include "utils/os.h"

#include <thread>

namespace logger
{
    namespace details
    {

        LogEvent::LogEvent(std::chrono::system_clock::time_point log_time,
                           const char *filename,
                           int line,
                           const char *funcname,
                           std::string_view logger_name,
                           LogLevel lvl,
                           std::string_view msg)
            : logger_name(logger_name), level(lvl), time(log_time), thread_id(utils::thread_id()), filename(filename), line(line), funcname(funcname), payload(msg)
        {
        }

    } // namespace details
} // namespace logger

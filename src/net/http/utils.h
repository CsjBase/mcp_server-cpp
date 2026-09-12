#pragma once

#include <string>
#include <time.h>

namespace net
{
    namespace http
    {
        std::string UrlDecode(const std::string &str, bool space_as_plus = true);
        std::string Time2Str(time_t ts = time(0), const std::string &format = "%Y-%m-%d %H:%M:%S");
        std::string Trim(const std::string &str, const std::string &delimit = " \t\r\n");
    }
}
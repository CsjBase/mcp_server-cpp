#include "public.h"

#include <algorithm>

namespace logger
{
    constexpr static std::string_view level_string_views[] LOG_LEVEL_NAMES;

    const std::string_view &to_string_view(const LogLevel &l)
    {
        return level_string_views[static_cast<int>(l)];
    }
    LogLevel from_str(const std::string &name)
    {
        auto it = std::find_if(std::begin(level_string_views), std::end(level_string_views),
                               [&name](const std::string_view &level_name)
                               {
                                   return level_name.size() == name.size() &&
                                          std::equal(name.begin(), name.end(), level_name.begin(),
                                                     [](char a, char b)
                                                     {
                                                         return std::tolower(static_cast<unsigned char>(a)) ==
                                                                std::tolower(static_cast<unsigned char>(b));
                                                     });
                               });
        if (it != std::end(level_string_views))
            return static_cast<LogLevel>(std::distance(std::begin(level_string_views), it));

        auto iequals = [](const std::string &a, const std::string &b)
        {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin(), [](char ac, char bc)
                              { return std::tolower(static_cast<unsigned char>(ac)) ==
                                       std::tolower(static_cast<unsigned char>(bc)); });
        };

        if (iequals(name, "warn"))
        {
            return LogLevel::Warn;
        }
        if (iequals(name, "err"))
        {
            return LogLevel::Error;
        }
        return LogLevel::Off;
    }
}

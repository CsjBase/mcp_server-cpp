#include "LogFormatter.h"

#include <chrono>

using namespace logger;

int main(int argc, char **argv)
{
    details::LogEvent event(std::chrono::system_clock::now(), __FILE_NAME__, __LINE__, __FUNCTION__, "test", LogLevel::Trace, "test msg");

    LogFormatter formatter;
    memory_buf_t dest;

    const std::vector<std::string> patterns = {
        "%+", "%l", "%a", "%A", "%b", "%B", "%c", "%Y", "%m", "%d",
        "%H", "%I", "%M", "%S", "%e", "%f", "%F", "%E", "%r", "%R",
        "%T", "%t", "%P", "%v", "%#", "%s", "%!", "%n", "%z", "%^",
        "%$", "%%"};

    for (const auto &pattern : patterns)
    {
        formatter.set_pattern(pattern);

        dest.clear();

        formatter.format(event, dest);

        fmt::print("{}:{}", pattern, fmt::string_view(dest.data(), dest.size()));
        fmt::print("====================================================\n");
    }

    return 0;
}
#include "net/base/Timestamp.h"

#include <chrono>
// #include <iomanip>
// #include <ctime>

namespace net
{
    Timestamp::Timestamp()
        : microSecondsSinceEpoch_(0)
    {
    }

    Timestamp::Timestamp(int64_t microSecondsSinceEpoch)
        : microSecondsSinceEpoch_(microSecondsSinceEpoch)
    {
    }

    Timestamp Timestamp::now()
    {
        // 获取当前时间点
        auto now = std::chrono::system_clock::now();
        // 计算自 Unix 纪元以来的时间间隔
        auto duration = now.time_since_epoch();
        // 将时间间隔转换为微秒数
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        // 返回 Timestamp 对象
        return Timestamp(microseconds);
    }

    std::string Timestamp::toString() const
    {
        int64_t seconds = microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
        int64_t microseconds = microSecondsSinceEpoch_ % kMicroSecondsPerSecond;

        if (microseconds < 0)
        {
            seconds -= 1;
            microseconds += kMicroSecondsPerSecond;
        }

        // Convert seconds since epoch to time_t
        std::time_t time = static_cast<std::time_t>(seconds);

        // Convert time_t to tm as local time
        std::tm *localTime = std::localtime(&time);

        // Format the string as "YYYY-MM-DD HH:MM:SS"
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localTime);

        return std::string(buffer);
    }

    int64_t Timestamp::toSeconds()
    {
        return microSecondsSinceEpoch_ / kMicroSecondsPerSecond;
    }

    Timestamp Timestamp::addSeconds(Timestamp timestamp, double seconds)
    {
        int64_t delta = static_cast<int64_t>(seconds * kMicroSecondsPerSecond);
        return Timestamp(timestamp.microSecondsSinceEpoch_ + delta);
    }
}
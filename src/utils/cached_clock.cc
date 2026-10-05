#include "utils/cached_clock.h"

#include <chrono>

namespace utils
{

    namespace
    {
        constexpr auto kRefreshInterval = std::chrono::milliseconds(1);
    }

    CachedClock &CachedClock::instance()
    {
        static CachedClock s_instance;
        return s_instance;
    }

    CachedClock::CachedClock()
    {
        // 构造即刷新一次, 避免首个读者拿到 0 值
        wall_ns_.store(std::chrono::system_clock::now().time_since_epoch().count(),
                       std::memory_order_relaxed);
        steady_ns_.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                         std::memory_order_relaxed);
        ticker_ = std::thread([this] { tickerLoop(); });
    }

    CachedClock::~CachedClock()
    {
        running_.store(false, std::memory_order_relaxed);
        if (ticker_.joinable())
            ticker_.join();
    }

    void CachedClock::tickerLoop()
    {
        while (running_.load(std::memory_order_relaxed))
        {
            wall_ns_.store(std::chrono::system_clock::now().time_since_epoch().count(),
                           std::memory_order_relaxed);
            steady_ns_.store(std::chrono::steady_clock::now().time_since_epoch().count(),
                             std::memory_order_relaxed);
            std::this_thread::sleep_for(kRefreshInterval);
        }
    }

} // namespace utils

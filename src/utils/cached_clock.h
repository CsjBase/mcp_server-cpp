#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace utils
{

    /**
     * @brief 后台时钟缓存(进程内单例)
     *
     * 独立后台线程每 ~1ms 刷新两个原子时间戳(墙钟 + 单调钟),
     * 读路径仅一次无锁原子加载(纳秒级)。
     *
     * 动机: clock_gettime / system_clock::now() 在部分虚拟化环境下
     * 因 VM 拦截(缺 vDSO 快路径)单次开销可达 ~20us(实测), 对
     * 每条日志/每次借还都取时间的热路径是灾难性的;
     * 本类把真实取时收敛到唯一后台线程, 热点线程零系统调用。
     *
     * 精度: 时间戳粒度为刷新周期(~1ms), 仅适用于毫秒级及以上
     * 精度需求的场景(日志时间戳、秒级超时判定等);
     * 需要高精度/一次性精确计时请直接用真实时钟。
     *
     * 线程安全: 原子读, 任意线程可并发调用。
     * 生命周期: 惰性初始化; 使用方(DBPool/LoggerManager 等)构造
     * 过程中即会调用本类, 保证本类先于使用方构造、后于其析构。
     */
    class CachedClock
    {
    public:
        static CachedClock &instance();

        /// 缓存墙钟(system_clock 语义), 粒度 ~1ms
        std::chrono::system_clock::time_point wall_now() const
        {
            return std::chrono::system_clock::time_point() +
                   std::chrono::nanoseconds(wall_ns_.load(std::memory_order_relaxed));
        }

        /// 缓存单调钟(steady_clock 语义), 粒度 ~1ms
        std::chrono::steady_clock::time_point steady_now() const
        {
            return std::chrono::steady_clock::time_point() +
                   std::chrono::nanoseconds(steady_ns_.load(std::memory_order_relaxed));
        }

        CachedClock(const CachedClock &) = delete;
        CachedClock &operator=(const CachedClock &) = delete;

    private:
        CachedClock();
        ~CachedClock();

        void tickerLoop();

        std::atomic<int64_t> wall_ns_{0};
        std::atomic<int64_t> steady_ns_{0};
        std::atomic<bool> running_{true};
        std::thread ticker_;
    };

    /// 便捷函数: 缓存墙钟 / 缓存单调钟
    inline std::chrono::system_clock::time_point cached_wall_now()
    {
        return CachedClock::instance().wall_now();
    }

    inline std::chrono::steady_clock::time_point cached_steady_now()
    {
        return CachedClock::instance().steady_now();
    }

} // namespace utils

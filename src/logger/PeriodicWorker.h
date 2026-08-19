#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace logger
{

    class PeriodicWorker
    {
    public:
        template <typename Rep, typename Period>
        PeriodicWorker(const std::function<void()> &callback_fun,
                       std::chrono::duration<Rep, Period> interval)
        {
            active_ = (interval > std::chrono::duration<Rep, Period>::zero());
            if (!active_)
            {
                return;
            }

            worker_thread_ = std::thread([this, callback_fun, interval]()
                                         {
                    for (;;) {
                        std::unique_lock<std::mutex> lock(this->mutex_);
                        if (this->cv_.wait_for(lock, interval, [this] { return !this->active_; })) {
                            return;  // active_ == false, so exit this thread
                        }
                        callback_fun();
                    } });
        }
        std::thread &get_thread() { return worker_thread_; }
        PeriodicWorker(const PeriodicWorker &) = delete;
        PeriodicWorker &operator=(const PeriodicWorker &) = delete;
        // stop the worker thread and join it
        ~PeriodicWorker();

    private:
        bool active_;
        std::thread worker_thread_;
        std::mutex mutex_;
        std::condition_variable cv_;
    };

}
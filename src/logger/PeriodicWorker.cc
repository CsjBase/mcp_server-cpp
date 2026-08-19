#include "logger/PeriodicWorker.h"

namespace logger
{
    PeriodicWorker::~PeriodicWorker()
    {
        if (worker_thread_.joinable())
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                active_ = false;
            }
            cv_.notify_one();
            worker_thread_.join();
        }
    }
}

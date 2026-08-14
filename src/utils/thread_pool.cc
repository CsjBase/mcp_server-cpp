#include "utils/thread_pool.h"

namespace utils
{

    ThreadPool::ThreadPool(size_t num_threads, size_t queue_size)
        : queue_(queue_size)
    {
        if (num_threads == 0)
            throw std::invalid_argument("ThreadPool: num_threads must be > 0");
        if (queue_size == 0)
            throw std::invalid_argument("ThreadPool: queue_size must be > 0");

        workers_.reserve(num_threads);
        for (size_t i = 0; i < num_threads; ++i)
            workers_.emplace_back(&ThreadPool::worker_loop_, this);
    }

    ThreadPool::~ThreadPool()
    {
        stop_gracefully();
    }

    void ThreadPool::stop_gracefully()
    {
        queue_.stop_gracefully();
        for (auto &w : workers_)
        {
            if (w.joinable())
                w.join();
        }
    }

    void ThreadPool::stop_immediately()
    {
        queue_.stop_immediately();
        for (auto &w : workers_)
        {
            if (w.joinable())
                w.join();
        }
    }

    void ThreadPool::worker_loop_()
    {
        while (true)
        {
            auto task = queue_.dequeue();
            if (!task)
                return;

            active_count_.fetch_add(1, std::memory_order_relaxed);
            (*task)();
            active_count_.fetch_sub(1, std::memory_order_relaxed);
            completed_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    size_t ThreadPool::pending_count() const
    {
        return queue_.size();
    }

    size_t ThreadPool::completed_count() const noexcept
    {
        return completed_count_.load(std::memory_order_relaxed);
    }

    void ThreadPool::reset_stats()
    {
        completed_count_.store(0, std::memory_order_relaxed);
        queue_.reset_overrun_counter();
        queue_.reset_discard_counter();
    }

    ThreadPool::Stats ThreadPool::get_stats() const
    {
        Stats s;
        s.pending_tasks = queue_.size();
        s.completed_tasks = completed_count_.load(std::memory_order_relaxed);
        s.active_threads = active_count_.load(std::memory_order_relaxed);
        s.total_threads = workers_.size();
        s.discarded_tasks = queue_.discard_counter();
        s.overrun_tasks = queue_.overrun_counter();
        return s;
    }

} // namespace utils

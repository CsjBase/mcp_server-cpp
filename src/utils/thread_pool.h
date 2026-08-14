#pragma once

#include "utils/mpmc_blocking_queue.h"

#include <atomic>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace utils
{

    /// 通用线程池：固定线程数 + 有界任务队列。
    /// 支持阻塞提交、覆盖最旧任务、非阻塞提交三种入队策略。
    class ThreadPool
    {
    public:
        using Task = std::function<void()>;

        /// @param num_threads 工作线程数，必须 > 0
        /// @param queue_size 任务队列最大容量，必须 > 0
        explicit ThreadPool(size_t num_threads, size_t queue_size);
        ~ThreadPool();

        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(const ThreadPool &) = delete;
        ThreadPool(ThreadPool &&) = delete;
        ThreadPool &operator=(ThreadPool &&) = delete;

        /// 优雅停止：worker 排空队列中剩余任务后退出，不再接收新任务
        void stop_gracefully();
        /// 立即停止：丢弃队列中剩余任务，worker 立即退出
        void stop_immediately();

        /// 提交任务，队列满时阻塞等待。停止后提交的任务将不执行。
        template <typename F, typename... Args>
        bool submit(F &&f, Args &&...args);

        /// 提交任务，队列满时覆盖最旧任务。停止后提交的任务将不执行。
        template <typename F, typename... Args>
        bool submit_overwrite(F &&f, Args &&...args);

        /// 非阻塞提交，队列满时立即返回 false
        template <typename F, typename... Args>
        bool try_submit(F &&f, Args &&...args);

        size_t pending_count() const;
        size_t completed_count() const noexcept;
        void reset_stats();

        struct Stats
        {
            size_t pending_tasks = 0;
            size_t completed_tasks = 0;
            size_t active_threads = 0;
            size_t total_threads = 0;
            size_t discarded_tasks = 0;
            size_t overrun_tasks = 0;
        };
        Stats get_stats() const;

    private:
        void worker_loop_();

        std::vector<std::thread> workers_;
        mpmc_blocking_queue<Task> queue_;
        std::atomic<size_t> active_count_{0};
        std::atomic<size_t> completed_count_{0};
    };

    // ---- template implementations ----

    template <typename F, typename... Args>
    bool ThreadPool::submit(F &&f, Args &&...args)
    {
        return queue_.enqueue(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

    template <typename F, typename... Args>
    bool ThreadPool::submit_overwrite(F &&f, Args &&...args)
    {
        return queue_.enqueue_overwrite(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

    template <typename F, typename... Args>
    bool ThreadPool::try_submit(F &&f, Args &&...args)
    {
        return queue_.try_enqueue(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

} // namespace utils

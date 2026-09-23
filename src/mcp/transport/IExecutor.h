#pragma once

#include <functional>

#include "utils/thread_pool.h"

namespace mcp
{
    class IExecutor
    {
    public:
        virtual ~IExecutor() = default;
        virtual void execute(std::function<void()> task) = 0;
        virtual size_t pending_count() const = 0;
    };

    // ThreadPoolExecutor：生产环境
    class ThreadPoolExecutor : public IExecutor
    {
    public:
        explicit ThreadPoolExecutor(int threads, size_t queue_limit)
            : pool_(threads, queue_limit)
        {
        }
        void execute(std::function<void()> task) override
        {
            pool_.submit(std::move(task));
        }
        size_t pending_count() const override { return pool_.pending_count(); }
        void stop() { pool_.stop_gracefully(); }

    private:
        utils::ThreadPool pool_;
    };

    // SynchronousExecutor：测试环境
    class SynchronousExecutor : public IExecutor
    {
    public:
        void execute(std::function<void()> task) override { task(); }
        size_t pending_count() const override { return 0; }
    };

} // namespace mcp
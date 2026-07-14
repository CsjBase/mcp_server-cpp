#pragma once

#include "utils/mpmc_blocking_queue.h"
#include "logger/LogEvent.h"
#include "logger/AsyncLogger.h"

#include <vector>
#include <thread>

namespace logger
{
    class AsyncLogger;
    enum class AsyncEventType
    {
        Log,
        Flush,
        Stop
    };

    struct AsyncEvent
    {
        AsyncEvent() = default;
        AsyncEvent(AsyncEventType ev_type, std::shared_ptr<AsyncLogger> &&worker, details::LogEvent ev);
        AsyncEvent(AsyncEventType ev_type, std::shared_ptr<AsyncLogger> &&worker)
            : type(ev_type), worker_ptr(std::move(worker)), event{}
        {
        }
        explicit AsyncEvent(AsyncEventType ev_type)
            : AsyncEvent{ev_type, nullptr}
        {
        }

        ~AsyncEvent() = default;
        AsyncEvent(const AsyncEvent &) = delete;
        AsyncEvent(AsyncEvent &&);
        AsyncEvent &operator=(AsyncEvent &&);

        void update_string_views();

        AsyncEventType type{AsyncEventType::Log};
        std::shared_ptr<AsyncLogger> worker_ptr;
        details::LogEvent event;
        memory_buf_t buffer;
    };

    class ThreadPool
    {
    public:
        ThreadPool(size_t q_max_items, size_t num_threads);
        ~ThreadPool();
        ThreadPool(const ThreadPool &) = delete;
        ThreadPool &operator=(ThreadPool &&) = delete;

        void post_log(std::shared_ptr<AsyncLogger> &&worker, const details::LogEvent &ev, AsyncOverflowStrategy strategy);
        void post_flush(std::shared_ptr<AsyncLogger> &&worker, AsyncOverflowStrategy strategy);
        size_t overrun_count();
        void reset_overrun_counter();
        size_t discard_counter();
        void reset_discard_counter();
        size_t queue_size();

    private:
        void post_async_msg_(AsyncEvent &&new_msg, AsyncOverflowStrategy strategy);
        void worker_loop_();

    private:
        std::vector<std::thread> threads_;
        utils::mpmc_blocking_queue<AsyncEvent> queue_;
    };
}
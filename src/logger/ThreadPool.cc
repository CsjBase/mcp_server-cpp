#include "logger/ThreadPool.h"

namespace logger
{

    AsyncEvent::AsyncEvent(AsyncEventType ev_type, std::shared_ptr<AsyncLogger> &&worker, details::LogEvent ev)
        : type(ev_type), worker_ptr(std::move(worker)), event(std::move(ev))
    {
        buffer.append(ev.logger_name);
        buffer.append(ev.payload);
        update_string_views();
    }

    AsyncEvent::AsyncEvent(AsyncEvent &&other)
    {
        type = other.type;
        worker_ptr = std::move(other.worker_ptr);
        event = std::move(other.event);

        buffer.append(other.event.logger_name);
        buffer.append(other.event.payload);
        update_string_views();
    }
    AsyncEvent &AsyncEvent::operator=(AsyncEvent &&other)
    {
        type = other.type;
        worker_ptr = std::move(other.worker_ptr);
        event = other.event;

        buffer.clear();
        buffer.append(event.logger_name);
        buffer.append(event.payload);
        update_string_views();
        return *this;
    }

    void AsyncEvent::update_string_views()
    {
        event.logger_name = std::string_view(buffer.data(), event.logger_name.size());
        event.payload = std::string_view(buffer.data() + event.logger_name.size(), event.payload.size());
    }
    ThreadPool::ThreadPool(size_t q_max_items, size_t num_threads)
        : queue_(q_max_items)
    {
        if (num_threads <= 0 || num_threads > 1000)
        {
            throw LogException("spdlog::thread_pool(): invalid threads_n param (valid "
                               "range is 1-1000)");
        }
        for (int i = 0; i < num_threads; i++)
        {
            threads_.emplace_back([this]
                                  { worker_loop_(); });
        }
    }
    ThreadPool::~ThreadPool()
    {
        try
        {
            for (int i = 0; i < threads_.size(); i++)
            {
                post_async_msg_(AsyncEvent(AsyncEventType::Stop), AsyncOverflowStrategy::Block);
            }
            for (auto &t : threads_)
            {
                t.join();
            }
        }
        catch (const std::exception &)
        {
        }
    }

    void ThreadPool::post_log(std::shared_ptr<AsyncLogger> &&worker, const details::LogEvent &ev, AsyncOverflowStrategy strategy)
    {
        AsyncEvent async_event(AsyncEventType::Log, std::move(worker), std::move(ev));
        post_async_msg_(std::move(async_event), strategy);
    }
    void ThreadPool::post_flush(std::shared_ptr<AsyncLogger> &&worker, AsyncOverflowStrategy strategy)
    {
        post_async_msg_(AsyncEvent(AsyncEventType::Flush, std::move(worker)), strategy);
    }
    size_t ThreadPool::overrun_count()
    {
        return queue_.overrun_counter();
    }
    void ThreadPool::reset_overrun_counter()
    {
        queue_.reset_overrun_counter();
    }
    size_t ThreadPool::discard_counter()
    {
        return queue_.discard_counter();
    }
    void ThreadPool::reset_discard_counter()
    {
        queue_.reset_discard_counter();
    }
    size_t ThreadPool::queue_size()
    {
        return queue_.size();
    }

    void ThreadPool::post_async_msg_(AsyncEvent &&new_msg, AsyncOverflowStrategy strategy)
    {
        if (strategy == AsyncOverflowStrategy::Block)
        {
            queue_.enqueue(std::move(new_msg));
        }
        else if (strategy == AsyncOverflowStrategy::DiscardNew)
        {
            queue_.try_enqueue(std::move(new_msg));
        }
        else
        {
            queue_.enqueue_overwrite(std::move(new_msg));
        }
    }
    void ThreadPool::worker_loop_()
    {
        bool running = true;
        while (running)
        {
            AsyncEvent async_event = std::move(queue_.dequeue());
            switch (async_event.type)
            {
            case AsyncEventType::Log:
                async_event.worker_ptr->backend_sink_it_(async_event.event);
                break;
            case AsyncEventType::Flush:
                async_event.worker_ptr->backend_flush_();
                break;
            case AsyncEventType::Stop:
                running = false;
                break;
            default:
                break;
            }
        }
    }

} // namespace logger

#pragma once

#include "logger/AsyncLogger.h"

namespace logger
{
    namespace details
    {
        static const size_t DefaultAsyncQueueSize = 8192;
    }

    template <AsyncOverflowStrategy OverflowStrategy = AsyncOverflowStrategy::Block>
    struct AsynchronousFactoryImp
    {
        template <typename Sink, typename... SinkArgs>
        static std::shared_ptr<Logger> create(const std::string &logger_name, SinkArgs &&...args)
        {
            auto &manager = LoggerManager::instance();

            auto &thread_pool_mutex = manager.get_thread_pool_mutex();
            std::lock_guard<std::recursive_mutex> thread_pool_lock(thread_pool_mutex);
            auto tp = manager.get_thread_pool();
            if (tp == nullptr)
            {
                tp = std::make_shared<utils::ThreadPool>(1, details::DefaultAsyncQueueSize);
                manager.set_thread_pool(tp);
            }
            auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);

            auto logger = std::make_shared<AsyncLogger>(std::move(logger_name), std::move(sink), std::move(tp), OverflowStrategy);
            manager.initialize_and_add_logger(logger);
            return logger;
        }

        static std::shared_ptr<Logger> create(const std::string &logger_name, std::vector<std::shared_ptr<LogSink>> sinks)
        {
            auto &manager = LoggerManager::instance();

            auto &thread_pool_mutex = manager.get_thread_pool_mutex();
            std::lock_guard<std::recursive_mutex> thread_pool_lock(thread_pool_mutex);
            auto tp = manager.get_thread_pool();
            if (tp == nullptr)
            {
                tp = std::make_shared<utils::ThreadPool>(1, details::DefaultAsyncQueueSize);
                manager.set_thread_pool(tp);
            }

            auto logger = std::make_shared<AsyncLogger>(std::move(logger_name), std::move(sinks), std::move(tp), OverflowStrategy);
            manager.initialize_and_add_logger(logger);
            return logger;
        }

        template <typename It>
        static std::shared_ptr<Logger> create(const std::string &logger_name, It begin, It end)
        {
            auto &manager = LoggerManager::instance();

            auto &thread_pool_mutex = manager.get_thread_pool_mutex();
            std::lock_guard<std::recursive_mutex> thread_pool_lock(thread_pool_mutex);
            auto tp = manager.get_thread_pool();
            if (tp == nullptr)
            {
                tp = std::make_shared<utils::ThreadPool>(1, details::DefaultAsyncQueueSize);
                manager.set_thread_pool(tp);
            }

            auto logger = std::make_shared<AsyncLogger>(std::move(logger_name), begin, end, std::move(tp), OverflowStrategy);
            manager.initialize_and_add_logger(logger);
            return logger;
        }
    };

    using AsynchronousFactory = AsynchronousFactoryImp<AsyncOverflowStrategy::Block>;
    using AsynchronousFactoryNonblocking = AsynchronousFactoryImp<AsyncOverflowStrategy::DiscardOldest>;

    template <typename Sink, typename... SinkArgs>
    std::shared_ptr<Logger> create_async_logger(const std::string &logger_name, SinkArgs &&...args)
    {
        return AsynchronousFactory::create<Sink>(logger_name, std::forward<SinkArgs>(args)...);
    }

    template <typename Sink, typename... SinkArgs>
    std::shared_ptr<Logger> create_async_logger_nonblocking(const std::string &logger_name, SinkArgs &&...args)
    {
        return AsynchronousFactoryNonblocking::create<Sink>(logger_name, std::forward<SinkArgs>(args)...);
    }

    inline std::shared_ptr<Logger> create_async_logger(const std::string &logger_name, std::vector<std::shared_ptr<LogSink>> sinks)
    {
        return AsynchronousFactory::create(logger_name, std::move(sinks));
    }

    inline std::shared_ptr<Logger> create_async_logger_nonblocking(const std::string &logger_name, std::vector<std::shared_ptr<LogSink>> sinks)
    {
        return AsynchronousFactoryNonblocking::create(logger_name, std::move(sinks));
    }

    template <typename It>
    std::shared_ptr<Logger> create_async_logger(const std::string &logger_name, It begin, It end)
    {
        return AsynchronousFactory::create(logger_name, begin, end);
    }

    template <typename It>
    std::shared_ptr<Logger> create_async_logger_nonblocking(const std::string &logger_name, It begin, It end)
    {
        return AsynchronousFactoryNonblocking::create(logger_name, begin, end);
    }
}

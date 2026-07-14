#pragma once

#include "logger/Logger.h"

namespace logger
{
    class ThreadPool;
    enum class AsyncOverflowStrategy
    {
        Block,
        DiscardNew,
        DiscardOldest
    };
    class AsyncLogger : public Logger, public std::enable_shared_from_this<AsyncLogger>
    {
    public:
        friend class ThreadPool;
        template <typename It>
        AsyncLogger(std::string name, It begin, It end,
                    std::shared_ptr<ThreadPool> tp,
                    AsyncOverflowStrategy overflow_strategy = AsyncOverflowStrategy::Block)
            : Logger(std::move(name), begin, end),
              thread_pool_(std::move(tp)),
              overflow_strategy_(overflow_strategy)
        {
        }

        AsyncLogger(std::string name, std::shared_ptr<LogSink> sink,
                    std::shared_ptr<ThreadPool> tp,
                    AsyncOverflowStrategy overflow_strategy = AsyncOverflowStrategy::Block);
        AsyncLogger(std::string name, std::vector<std::shared_ptr<LogSink>> sinks,
                    std::shared_ptr<ThreadPool> tp,
                    AsyncOverflowStrategy overflow_strategy = AsyncOverflowStrategy::Block);

        std::shared_ptr<Logger> clone(std::string new_name) override;

    protected:
        void sink_it_(const details::LogEvent &) override;
        void flush_() override;
        void backend_sink_it_(const details::LogEvent &incoming_log_event);
        void backend_flush_();

    private:
        std::weak_ptr<ThreadPool> thread_pool_;
        AsyncOverflowStrategy overflow_strategy_{AsyncOverflowStrategy::Block};
    };

}
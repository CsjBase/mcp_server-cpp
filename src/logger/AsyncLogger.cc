#include "logger/AsyncLogger.h"
#include "logger/LogEventBuffer.h"

#include <nlohmann/json.hpp>

namespace logger
{
    AsyncLogger::AsyncLogger(
        std::string name, std::shared_ptr<LogSink> sink,
        std::shared_ptr<utils::ThreadPool> tp,
        AsyncOverflowStrategy overflow_strategy)
        : Logger(std::move(name), std::move(sink)), thread_pool_(std::move(tp)),
          overflow_strategy_(overflow_strategy)
    {
    }

    AsyncLogger::AsyncLogger(
        std::string name, std::vector<std::shared_ptr<LogSink>> sinks,
        std::shared_ptr<utils::ThreadPool> tp,
        AsyncOverflowStrategy overflow_strategy)
        : Logger(std::move(name), std::move(sinks)), thread_pool_(std::move(tp)),
          overflow_strategy_(overflow_strategy) {}
    std::shared_ptr<Logger> AsyncLogger::clone(std::string new_name)
    {
        auto cloned = std::make_shared<AsyncLogger>(*this);
        cloned->name_ = std::move(new_name);
        return cloned;
    }

    nlohmann::json AsyncLogger::toJson() const
    {
        nlohmann::json json{{"name", name_}, {"logger_type", "async"}};
        for (auto &sink : sinks_)
        {
            json["sinks"].push_back(sink->toJson());
        }
        return json;
    }

    std::string AsyncLogger::toJsonString() const
    {
        return toJson().dump();
    }

    void AsyncLogger::sink_it_(const details::LogEvent &event)
    {
        LOGGER_TRY
        {
            auto pool_ptr = thread_pool_.lock();
            if (!pool_ptr)
            {
                throw LogException("async logger:thread pool doesn't exist anymore");
            }

            auto task = [worker = shared_from_this(), event_buffer = details::LogEventBuffer(event)]()
            { worker->backend_sink_it_(event_buffer); };

            if (overflow_strategy_ == AsyncOverflowStrategy::Block)
            {
                pool_ptr->submit(std::move(task));
            }
            else if (overflow_strategy_ == AsyncOverflowStrategy::DiscardNew)
            {
                pool_ptr->try_submit(std::move(task));
            }
            else
            {
                pool_ptr->submit_overwrite(std::move(task));
            }
        }
        LOGGER_CATCH(event)
    }

    void AsyncLogger::flush_()
    {
        LOGGER_TRY
        {
            auto pool_ptr = thread_pool_.lock();
            if (!pool_ptr)
            {
                throw LogException("async logger:thread pool doesn't exist anymore");
            }

            auto task = [worker = shared_from_this()]()
            { worker->backend_flush_(); };

            if (overflow_strategy_ == AsyncOverflowStrategy::Block)
            {
                pool_ptr->submit(std::move(task));
            }
            else if (overflow_strategy_ == AsyncOverflowStrategy::DiscardNew)
            {
                pool_ptr->try_submit(std::move(task));
            }
            else
            {
                pool_ptr->submit_overwrite(std::move(task));
            }
        }
        LOGGER_CATCH(details::LogEvent{})
    }

    void AsyncLogger::backend_sink_it_(
        const details::LogEvent &incoming_log_event)
    {
        for (auto &sink : sinks_)
        {
            if (sink->should_log(incoming_log_event.level))
            {
                LOGGER_TRY { sink->log(incoming_log_event); }
                LOGGER_CATCH(incoming_log_event)
            }
        }

        if (incoming_log_event.level >= flush_level())
            flush_();
    }
    void AsyncLogger::backend_flush_()
    {
        for (auto &sink : sinks_)
        {
            LOGGER_TRY { sink->flush(); }
            LOGGER_CATCH(details::LogEvent{})
        }
    }

}
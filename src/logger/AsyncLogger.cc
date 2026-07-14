#include "logger/AsyncLogger.h"
#include "logger/ThreadPool.h"

namespace logger
{
  AsyncLogger::AsyncLogger(
      std::string name, std::shared_ptr<LogSink> sink,
      std::shared_ptr<ThreadPool> tp,
      AsyncOverflowStrategy overflow_strategy)
      : Logger(std::move(name), std::move(sink)), thread_pool_(std::move(tp)),
        overflow_strategy_(overflow_strategy)
  {
  }

  AsyncLogger::AsyncLogger(
      std::string name, std::vector<std::shared_ptr<LogSink>> sinks,
      std::shared_ptr<ThreadPool> tp,
      AsyncOverflowStrategy overflow_strategy)
      : Logger(std::move(name), std::move(sinks)), thread_pool_(std::move(tp)),
        overflow_strategy_(overflow_strategy) {}
  std::shared_ptr<Logger> AsyncLogger::clone(std::string new_name)
  {
    auto cloned = std::make_shared<AsyncLogger>(*this);
    cloned->name_ = std::move(new_name);
    return cloned;
  }

  void AsyncLogger::sink_it_(const details::LogEvent &event)
  {
    LOGGER_TRY
    {
      if (auto pool_ptr = thread_pool_.lock())
      {
        pool_ptr->post_log(shared_from_this(), event, overflow_strategy_);
      }
      else
      {
        throw LogException("async logger:thread pool doesn't exist anymore");
      }
    }
    LOGGER_CATCH(event)
  }

  void AsyncLogger::flush_()
  {
    LOGGER_TRY
    {
      if (auto pool_ptr = thread_pool_.lock())
      {
        pool_ptr->post_flush(shared_from_this(), overflow_strategy_);
      }
      else
      {
        throw LogException("async logger:thread pool doesn't exist anymore");
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
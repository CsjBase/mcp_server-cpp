#include "logger/LoggerManager.h"
#include "logger/sinks/ColorLogSink.h"

namespace logger
{
    LoggerManager &LoggerManager::instance()
    {
        static LoggerManager instance;
        return instance;
    }

    void LoggerManager::add_logger(std::shared_ptr<Logger> new_logger)
    {
        if (!new_logger)
        {
            throw LogException("Cannot add null logger");
        }

        std::string logger_name = new_logger->name();

        std::lock_guard<std::mutex> lock(mutex_);
        auto [it, success] = logger_map_.emplace(logger_name, std::move(new_logger));
        if (!success)
        {
            throw LogException("Logger with name '" + logger_name + "' already exists");
        }
    }

    void LoggerManager::add_or_replace_logger(std::shared_ptr<Logger> new_logger)
    {
        if (!new_logger)
        {
            throw LogException("Cannot add null logger");
        }

        std::string logger_name = new_logger->name();

        std::lock_guard<std::mutex> lock(mutex_);
        logger_map_[logger_name] = std::move(new_logger);
    }

    std::shared_ptr<Logger> LoggerManager::get_logger(std::string name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return logger_map_.find(name) != logger_map_.end() ? logger_map_[name] : nullptr;
    }
    std::shared_ptr<Logger> LoggerManager::get_default_logger()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return default_logger_;
    }

    void LoggerManager::set_default_logger(std::shared_ptr<Logger> logger)
    {
        if (logger == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        default_logger_ = std::move(logger);
    }
    void LoggerManager::set_thread_pool(std::shared_ptr<ThreadPool> pool)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        thread_pool_ = std::move(pool);
    }

    LoggerManager::LoggerManager()
    {
        auto color_sink = std::make_shared<StdoutColorLogSinkMT>();
        const char *default_logger_name = "";
        default_logger_ = std::make_shared<Logger>(default_logger_name, std::move(color_sink));
        logger_map_[default_logger_name] = default_logger_;
    }
    LoggerManager::~LoggerManager()
    {
    }
}
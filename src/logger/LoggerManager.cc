#include "logger/LoggerManager.h"
#include "logger/sinks/ColorLogSink.h"

namespace logger
{
    LoggerManager &LoggerManager::instance()
    {
        static LoggerManager instance;
        return instance;
    }

    void LoggerManager::initialize_and_add_logger(std::shared_ptr<Logger> new_logger)
    {
        if (!new_logger)
        {
            throw LogException("Cannot initialize_and_add null logger");
        }
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        new_logger->set_formatter(formatter_->clone());
        new_logger->set_level(global_log_level_);
        new_logger->set_flush_level(flush_level_);

        add_logger_(new_logger);
    }

    void LoggerManager::add_logger(std::shared_ptr<Logger> new_logger)
    {
        if (!new_logger)
        {
            throw LogException("Cannot add null logger");
        }
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        add_logger_(new_logger);
    }

    void LoggerManager::add_or_replace_logger(std::shared_ptr<Logger> new_logger)
    {
        if (!new_logger)
        {
            throw LogException("Cannot add_or_replace null logger");
        }

        std::string logger_name = new_logger->name();

        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        logger_map_[logger_name] = std::move(new_logger);
    }

    void LoggerManager::drop(const std::string &logger_name)
    {
        std::lock_guard<std::mutex> lock(logger_map_mutex_);

        auto is_default_logger = default_logger_ && default_logger_->name() == logger_name;
        logger_map_.erase(logger_name);
        if (is_default_logger)
        {
            default_logger_.reset();
        }
    }
    void LoggerManager::drop_all()
    {
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        logger_map_.clear();
        default_logger_.reset();
    }

    std::shared_ptr<Logger> LoggerManager::get_logger(std::string name)
    {
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        return logger_map_.find(name) != logger_map_.end() ? logger_map_[name] : nullptr;
    }
    std::shared_ptr<Logger> LoggerManager::get_default_logger()
    {
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        return default_logger_;
    }

    void LoggerManager::set_formatter(std::unique_ptr<LogFormatter> formatter)
    {
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        formatter_ = std::move(formatter);
        for (auto &l : logger_map_)
        {
            l.second->set_formatter(formatter_->clone());
        }
    }

    void LoggerManager::set_level(LogLevel level)
    {

        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        global_log_level_ = level;
        for (auto &l : logger_map_)
        {
            l.second->set_level(global_log_level_);
        }
    }

    void LoggerManager::set_flush_level(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        flush_level_ = level;
        for (auto &l : logger_map_)
        {
            l.second->set_flush_level(flush_level_);
        }
    }

    void LoggerManager::set_default_logger(std::shared_ptr<Logger> logger)
    {
        if (logger == nullptr)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        logger_map_[logger->name()] = logger;
        default_logger_ = std::move(logger);
    }
    void LoggerManager::set_thread_pool(std::shared_ptr<utils::ThreadPool> pool)
    {
        std::lock_guard<std::recursive_mutex> lock(thread_pool_mutex_);
        thread_pool_ = std::move(pool);
    }

    std::shared_ptr<utils::ThreadPool> LoggerManager::get_thread_pool()
    {
        std::lock_guard<std::recursive_mutex> lock(thread_pool_mutex_);
        return thread_pool_;
    }

    std::recursive_mutex &LoggerManager::get_thread_pool_mutex()
    {
        return thread_pool_mutex_;
    }

    void LoggerManager::flush_all()
    {
        std::lock_guard<std::mutex> lock(logger_map_mutex_);
        for (auto &[name, logger] : logger_map_)
        {
            logger->flush();
        }
    }

    void LoggerManager::shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(flusher_mutex_);
            periodic_flusher_.reset();
        }

        drop_all();

        {
            std::lock_guard<std::recursive_mutex> lock(thread_pool_mutex_);
            thread_pool_.reset();
        }
    }

    LoggerManager::LoggerManager()
        : formatter_(new LogFormatter)
    {
        auto color_sink = std::make_shared<StdoutColorLogSinkMT>();
        const char *default_logger_name = "";
        default_logger_ = std::make_shared<Logger>(default_logger_name, std::move(color_sink));
        logger_map_[default_logger_name] = default_logger_;
    }
    LoggerManager::~LoggerManager()
    {
    }

    void LoggerManager::add_logger_(std::shared_ptr<Logger> new_logger)
    {
        std::string logger_name = new_logger->name();
        auto [it, success] = logger_map_.emplace(logger_name, std::move(new_logger));
        if (!success)
        {
            throw LogException("Logger with name '" + logger_name + "' already exists");
        }
    }
}
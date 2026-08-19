#pragma once

#include "logger/Logger.h"
#include "utils/thread_pool.h"
#include "logger/PeriodicWorker.h"

#include <mutex>

namespace logger
{
    class LoggerManager
    {
    public:
        static LoggerManager &instance();

        LoggerManager(const LoggerManager &) = delete;
        LoggerManager &operator=(const LoggerManager &) = delete;

        void initialize_and_add_logger(std::shared_ptr<Logger> new_logger);
        void add_logger(std::shared_ptr<Logger> new_logger);
        void add_or_replace_logger(std::shared_ptr<Logger> new_logger);
        void drop(const std::string &logger_name);
        void drop_all();
        std::shared_ptr<Logger> get_logger(std::string name);
        std::shared_ptr<Logger> get_default_logger();

        void set_formatter(std::unique_ptr<LogFormatter> formatter);
        void set_level(LogLevel level);
        void set_flush_level(LogLevel level);
        void set_default_logger(std::shared_ptr<Logger> logger);

        void set_thread_pool(std::shared_ptr<utils::ThreadPool> pool);
        std::shared_ptr<utils::ThreadPool> get_thread_pool();
        std::recursive_mutex &get_thread_pool_mutex();

        template <typename Rep, typename Period>
        void flush_every(std::chrono::duration<Rep, Period> interval)
        {
            std::lock_guard<std::mutex> lock(flusher_mutex_);
            auto periodic_callback = [this]()
            {
                flush_all();
            };
            periodic_flusher_ = std::make_unique<PeriodicWorker>(periodic_callback, interval);
        }
        void flush_all();

        void shutdown();

    private:
        LoggerManager();
        ~LoggerManager();
        void add_logger_(std::shared_ptr<Logger> new_logger);

    private:
        std::mutex logger_map_mutex_;
        std::shared_ptr<Logger> default_logger_;
        std::unique_ptr<LogFormatter> formatter_;
        LogLevel global_log_level_ = LogLevel::Info;
        LogLevel flush_level_ = LogLevel::Off;
        std::unordered_map<std::string, std::shared_ptr<Logger>> logger_map_;

        std::recursive_mutex thread_pool_mutex_;
        std::shared_ptr<utils::ThreadPool> thread_pool_;

        std::mutex flusher_mutex_;
        std::unique_ptr<PeriodicWorker> periodic_flusher_;
    };
}

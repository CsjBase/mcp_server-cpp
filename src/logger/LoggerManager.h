#pragma once

#include "logger/Logger.h"
#include "logger/ThreadPool.h"

#include <mutex>

namespace logger
{
    class LoggerManager
    {
    public:
        static LoggerManager &instance();

        LoggerManager(const LoggerManager &) = delete;
        LoggerManager &operator=(const LoggerManager &) = delete;

        void add_logger(std::shared_ptr<Logger> new_logger);
        void add_or_replace_logger(std::shared_ptr<Logger> new_logger);
        std::shared_ptr<Logger> get_logger(std::string name);
        std::shared_ptr<Logger> get_default_logger();

        void set_default_logger(std::shared_ptr<Logger> logger);
        void set_thread_pool(std::shared_ptr<ThreadPool> pool);

    private:
        LoggerManager();
        ~LoggerManager();

    private:
        std::mutex mutex_;
        std::shared_ptr<Logger> default_logger_;

        std::unordered_map<std::string, std::shared_ptr<Logger>> logger_map_;

        std::shared_ptr<ThreadPool> thread_pool_;
    };
}

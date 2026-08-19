#pragma once

#include "logger/LoggerManager.h"

namespace logger
{

    class Logger;
    struct SynchronousFactory
    {
        template <typename Sink, typename... SinkArgs>
        static std::shared_ptr<Logger> create(const std::string &logger_name, SinkArgs &&...args)
        {
            auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);
            auto new_logger = std::make_shared<Logger>(logger_name, std::move(sink));
            LoggerManager::instance().initialize_and_add_logger(new_logger);
            return new_logger;
        }
    };

}
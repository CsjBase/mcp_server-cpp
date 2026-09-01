#pragma once

#include "logger/LoggerManager.h"

namespace logger
{

    class Logger;
    struct SynchronousFactory
    {
        // template <typename Sink, typename... SinkArgs,
        //           std::enable_if_t<std::is_constructible_v<Sink, SinkArgs...>, int> = 0>
        template <typename Sink, typename... SinkArgs>
        static std::shared_ptr<Logger> create(const std::string &logger_name, SinkArgs &&...args)
        {
            auto sink = std::make_shared<Sink>(std::forward<SinkArgs>(args)...);
            auto new_logger = std::make_shared<Logger>(logger_name, std::move(sink));
            LoggerManager::instance().initialize_and_add_logger(new_logger);
            return new_logger;
        }

        static std::shared_ptr<Logger> create(const std::string &logger_name, std::vector<std::shared_ptr<LogSink>> sinks)
        {
            auto new_logger = std::make_shared<Logger>(logger_name, std::move(sinks));
            LoggerManager::instance().initialize_and_add_logger(new_logger);
            return new_logger;
        }

        // template <typename It,
        //           std::enable_if_t<std::is_base_of_v<std::input_iterator_tag,
        //                                              typename std::iterator_traits<It>::iterator_category>,
        //                            int> = 0>
        template <typename It>
        static std::shared_ptr<Logger> create(const std::string &logger_name, It begin, It end)
        {
            auto new_logger = std::make_shared<Logger>(logger_name, begin, end);
            LoggerManager::instance().initialize_and_add_logger(new_logger);
            return new_logger;
        }
    };

    // template <typename Sink, typename... SinkArgs,
    //           std::enable_if_t<std::is_constructible_v<Sink, SinkArgs...>, int> = 0>
    template <typename Sink, typename... SinkArgs>
    std::shared_ptr<Logger> create_logger(const std::string &logger_name, SinkArgs &&...args)
    {
        return SynchronousFactory::create<Sink>(logger_name, std::forward<SinkArgs>(args)...);
    }

    inline std::shared_ptr<Logger> create_logger(const std::string &logger_name, std::vector<std::shared_ptr<LogSink>> sinks)
    {
        return SynchronousFactory::create(logger_name, std::move(sinks));
    }

    // template <typename It,
    //           std::enable_if_t<std::is_base_of_v<std::input_iterator_tag,
    //                                              typename std::iterator_traits<It>::iterator_category>,
    //                            int> = 0>
    template <typename It>
    std::shared_ptr<Logger> create_logger(const std::string &logger_name, It begin, It end)
    {
        return SynchronousFactory::create(logger_name, begin, end);
    }

}
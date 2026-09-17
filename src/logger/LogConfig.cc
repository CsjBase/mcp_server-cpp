#include "LogConfig.h"
#include "logger/log.h"
#include "logger/AsynchronousFactory.h"
#include "logger/SynchronousFactory.h"

#include "logger/sinks/StdoutLogSink.h"
#include "logger/sinks/ColorLogSink.h"
#include "logger/sinks/BasicFileLogSink.h"
#include "logger/sinks/DailyFileLogSink.h"
#include "logger/sinks/RotatingFileLogSink.h"

namespace logger
{

    constexpr static std::string_view sinktype_string_views[]{
#define XX(type, name, len) std::string_view(#name, len),
        SINK_TYPES(XX)
#undef XX
            std::string_view("unknown")};

    const std::string_view &LogSinkConfig::to_string_view(const LogSinkConfig::Type &t)
    {
        return sinktype_string_views[static_cast<int>(t)];
    }

    LogSinkConfig::Type LogSinkConfig::from_str(const std::string &name)
    {
        auto it = std::find_if(std::begin(sinktype_string_views), std::end(sinktype_string_views),
                               [&name](const std::string_view &level_name)
                               {
                                   return level_name.size() == name.size() &&
                                          std::equal(name.begin(), name.end(), level_name.begin(),
                                                     [](char a, char b)
                                                     {
                                                         return std::tolower(static_cast<unsigned char>(a)) ==
                                                                std::tolower(static_cast<unsigned char>(b));
                                                     });
                               });
        if (it != std::end(sinktype_string_views))
            return static_cast<LogSinkConfig::Type>(std::distance(std::begin(sinktype_string_views), it));
        return LogSinkConfig::Type::UNKNOWN;
    }

    void to_json(json &j, const LogSinkConfig &s)
    {
        j = {
            {"level", to_string_view(s.level)},
            {"pattern", s.pattern},
            {"type", LogSinkConfig::to_string_view(s.type)}};
        if (!s.file.empty())
        {
            j["file"] = s.file;
        }
        if (s.type == LogSinkConfig::Type::DAILYFILE_MT || s.type == LogSinkConfig::Type::DAILYFILE_ST || s.type == LogSinkConfig::Type::ROTAINGFILE_MT || s.type == LogSinkConfig::Type::ROTAINGFILE_ST)
        {
            j["max_files"] = s.max_files;
        }
    }

    void from_json(const json &j, LogSinkConfig &s)
    {
        s.type = LogSinkConfig::from_str(j.at("type").get<std::string>());
        s.level = from_str(j.at("level").get<std::string>());
        if (j.contains("pattern"))
        {
            s.pattern = j.at("pattern").get<std::string>();
        }
        else
        {
            s.pattern = "%+";
        }

        if (j.contains("file"))
        {
            s.file = j.at("file").get<std::string>();
        }

        if (j.contains("max_files"))
        {
            s.max_files = j.at("max_files").get<size_t>();
        }

        if (j.contains("max_size"))
        {
            s.max_size = j.at("max_size").get<size_t>();
        }
    }

    void to_json(json &j, const LoggerConfig &l)
    {
        j = {
            {"name", l.name},
            {"logger_type", l.logger_type == LoggerConfig::LoggerType::Sync ? "sync" : "async"},
            {"sinks", l.sinks}};
    }
    void from_json(const json &j, LoggerConfig &l)
    {
        l.name = j.at("name").get<std::string>();
        l.sinks = j.at("sinks").get<std::vector<LogSinkConfig>>();
        if (j.contains("logger_type"))
        {
            l.logger_type = j.at("logger_type").get<std::string>() == "sync"
                                ? LoggerConfig::LoggerType::Sync
                                : LoggerConfig::LoggerType::Async;
        }
        else
        {
            l.logger_type = LoggerConfig::LoggerType::Sync;
        }
    };
    config::ConfigVar<std::map<std::string, LoggerConfig>>::ptr g_logger_config =
        config::Config::lookup("logs", std::map<std::string, LoggerConfig>(), "logger config");

    struct LogIniter
    {
        static std::shared_ptr<LogSink> create_sink(const LogSinkConfig &config)
        {
            std::shared_ptr<LogSink> sink;
            switch (config.type)
            {
            case LogSinkConfig::Type::STDOUT_MT:
                sink = std::make_shared<StdoutLogSinkMT>();
                break;
            case LogSinkConfig::Type::STDOUT_ST:
                sink = std::make_shared<StdoutLogSinkST>();
                break;
            case LogSinkConfig::Type::STDOUT_COLOR_MT:
                sink = std::make_shared<StdoutColorLogSinkMT>();
                break;
            case LogSinkConfig::Type::STDOUT_COLOR_ST:
                sink = std::make_shared<StdoutColorLogSinkST>();
                break;
            case LogSinkConfig::Type::STDERR_MT:
                sink = std::make_shared<StderrLogSinkMT>();
                break;
            case LogSinkConfig::Type::STDERR_ST:
                sink = std::make_shared<StderrLogSinkST>();
                break;
            case LogSinkConfig::Type::STDERR_COLOR_MT:
                sink = std::make_shared<StderrColorLogSinkMT>();
                break;
            case LogSinkConfig::Type::STDERR_COLOR_ST:
                sink = std::make_shared<StderrColorLogSinkST>();
                break;
            case LogSinkConfig::Type::BASICFILE_MT:
                sink = std::make_shared<BasicFileLogSinkMT>(config.file);
                break;
            case LogSinkConfig::Type::BASICFILE_ST:
                sink = std::make_shared<BasicFileLogSinkST>(config.file);
                break;
            case LogSinkConfig::Type::DAILYFILE_MT:
                sink = std::make_shared<DailyFileLogSinkMT>(config.file, 0, 0, false, config.max_files);
                break;
            case LogSinkConfig::Type::DAILYFILE_ST:
                sink = std::make_shared<DailyFileLogSinkST>(config.file, 0, 0, false, config.max_files);
                break;
            case LogSinkConfig::Type::ROTAINGFILE_MT:
                sink = std::make_shared<RotatingFileLogSinkMT>(config.file, config.max_size, config.max_files);
                break;
            case LogSinkConfig::Type::ROTAINGFILE_ST:
                sink = std::make_shared<RotatingFileLogSinkST>(config.file, config.max_size, config.max_files);
                break;

            default:
                sink = std::make_shared<StdoutColorLogSinkMT>();
                break;
            }
            return sink;
        }
        LogIniter()
        {
            g_logger_config->addChangeCallback([](const std::map<std::string, LoggerConfig> &old_value,
                                                  const std::map<std::string, LoggerConfig> &new_value)
                                               {
                                                std::cout << "LogIniter::LogIniter()" << std::endl;
                for(auto &i:new_value){
                    std::vector<std::shared_ptr<LogSink>> sinks;
                    sinks.reserve(i.second.sinks.size());
                    for(auto &j:i.second.sinks){
                        sinks.push_back(create_sink(j));
                    }
                    auto it = old_value.find(i.first);
                    Logger::ptr logger;
                    if(it == old_value.end() && LOGGER_NAME(i.first) == nullptr){
                        if(i.second.logger_type == LoggerConfig::LoggerType::Sync){
                            logger = create_logger(i.first, sinks);
                        }
                        else {
                            logger = create_async_logger(i.first, sinks);
                        }
                    }else {
                        logger = LOGGER_NAME(i.first);
                        auto async = std::dynamic_pointer_cast<AsyncLogger>(logger);
                        if(async !=nullptr && i.second.logger_type == LoggerConfig::LoggerType::Sync){
                            LoggerManager::instance().drop(i.first);
                            logger = create_logger(i.first, sinks);
                        } else if(async ==nullptr && i.second.logger_type == LoggerConfig::LoggerType::Async){
                            LoggerManager::instance().drop(i.first);
                            logger = create_async_logger(i.first, sinks);
                        }
                    }
                    for(int idx=0;idx<sinks.size();++idx){
                        sinks[idx]->set_level(i.second.sinks[idx].level);
                        sinks[idx]->set_pattern(i.second.sinks[idx].pattern);
                    }
                    if(i.first=="default"){
                        LoggerManager::instance().set_default_logger(logger);
                    }
                } });
        }
    };

    void initLogConfig()
    {
        static LogIniter __s_log_initer;
    }
}

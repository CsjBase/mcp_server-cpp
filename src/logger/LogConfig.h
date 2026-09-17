#pragma once

#include "public.h"
#include "logger/sinks/LogSink.h"
#include "config/Config.h"

#include <vector>
#include <nlohmann/json.hpp>

/// type name name_len
#define SINK_TYPES(XX)                       \
    XX(STDOUT_MT, stdout_mt, 9)              \
    XX(STDOUT_ST, stdout_st, 9)              \
    XX(STDOUT_COLOR_MT, stdout_color_mt, 15) \
    XX(STDOUT_COLOR_ST, stdout_color_st, 15) \
    XX(STDERR_MT, stderr_mt, 9)              \
    XX(STDERR_ST, stderr_st, 9)              \
    XX(STDERR_COLOR_MT, stderr_color_mt, 15) \
    XX(STDERR_COLOR_ST, stderr_color_st, 15) \
    XX(BASICFILE_MT, basicfile_mt, 12)       \
    XX(BASICFILE_ST, basicfile_st, 12)       \
    XX(DAILYFILE_MT, dailyfile_mt, 12)       \
    XX(DAILYFILE_ST, dailyfile_st, 12)       \
    XX(ROTAINGFILE_MT, rotatingfile_mt, 15)  \
    XX(ROTAINGFILE_ST, rotatingfile_st, 15)

namespace logger
{
    using json = nlohmann::json;

    struct LogSinkConfig
    {
        enum class Type
        {
#define XX(name, str, len) name,
            SINK_TYPES(XX)
#undef XX
                UNKNOWN
        };
        static const std::string_view &to_string_view(const Type &t);
        static Type from_str(const std::string &name);
        Type type;

        // 公共项
        LogLevel level;
        std::string pattern; // 可选，默认%+
        // 文件类公共项
        std::string file;
        // RotatingFileLogSink 和 DailyFileLogSink 专有
        size_t max_files;

        // RotatingFileLogSink 专有
        size_t max_size;

        bool operator==(const LogSinkConfig &other) const
        {
            return type == other.type &&
                   level == other.level &&
                   pattern == other.pattern &&
                   file == other.file &&
                   max_files == other.max_files &&
                   max_size == other.max_size;
        }
    };

    struct LoggerConfig
    {
        enum class LoggerType
        {
            Sync,
            Async
        };
        std::string name;
        LoggerType logger_type;
        std::vector<LogSinkConfig> sinks;
        bool operator==(const LoggerConfig &other) const
        {
            return name == other.name &&
                   logger_type == other.logger_type &&
                   sinks == other.sinks;
        }
    };

    void to_json(json &j, const LogSinkConfig &s);
    void from_json(const json &j, LogSinkConfig &s);

    void to_json(json &j, const LoggerConfig &l);
    void from_json(const json &j, LoggerConfig &l);

    extern config::ConfigVar<std::map<std::string, LoggerConfig>>::ptr g_logger_config;

    void initLogConfig();
}
#pragma once

#include "DBConfig.h"

#include <string>
#include <stdint.h>
#include <nlohmann/json.hpp>

namespace db
{
    enum class DBType
    {
        MYSQL,
        UNKNOWN
    };

    inline DBType dbTypeFromString(const std::string &s)
    {
        if (s == "mysql")
            return DBType::MYSQL;
        return DBType::UNKNOWN;
    }

    inline std::string dbTypeToString(DBType t)
    {
        switch (t)
        {
        case DBType::MYSQL:
            return "mysql";
        default:
            return "unknown";
        }
    }

    struct DBPoolConfig
    {
        DBType db_type;
        DBConfig db_config;
        int max_pool_size;             // 最大连接数
        int min_idle;                  // 最小空闲连接数
        int get_connection_timeout_ms; // 获取连接超时时长
        int idle_timeout_seconds;      // 最大空闲时长（从上次使用开始）
        int max_life_time_seconds;     // 最大存活时间（从连接创建开始）
        int keepalive_seconds = 60;        // 空闲连接保活检测间隔
        int borrowed_timeout_seconds = 30; // 连接泄漏检测时长
        int scan_interval_ms = 1000;       // 维护扫描间隔

        bool operator==(const DBPoolConfig &other)
        {
            return db_type == other.db_type &&
                   db_config == other.db_config &&
                   max_pool_size == other.max_pool_size &&
                   min_idle == other.min_idle &&
                   get_connection_timeout_ms == other.get_connection_timeout_ms &&
                   idle_timeout_seconds == other.idle_timeout_seconds &&
                   max_life_time_seconds == other.max_life_time_seconds &&
                   keepalive_seconds == other.keepalive_seconds &&
                   borrowed_timeout_seconds == other.borrowed_timeout_seconds &&
                   scan_interval_ms == other.scan_interval_ms;
        }

        static DBPoolConfig defaultConfig();
    };

    void to_json(json &j, const DBPoolConfig &);
    void from_json(const json &j, DBPoolConfig &);

}
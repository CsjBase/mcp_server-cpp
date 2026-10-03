#include "DBPoolConfig.h"

namespace db
{

    void to_json(json &j, const DBPoolConfig &dbp)
    {
        j["db_type"] = dbTypeToString(dbp.db_type);
        j["db_config"] = dbp.db_config;
        j["max_pool_size"] = dbp.max_pool_size;
        j["min_idle"] = dbp.min_idle;
        j["get_connection_timeout_ms"] = dbp.get_connection_timeout_ms;
        j["idle_timeout_seconds"] = dbp.idle_timeout_seconds;
        j["max_life_time_seconds"] = dbp.max_life_time_seconds;
        j["keepalive_seconds"] = dbp.keepalive_seconds;
        j["borrowed_timeout_seconds"] = dbp.borrowed_timeout_seconds;
        j["scan_interval_ms"] = dbp.scan_interval_ms;
    }

    void from_json(const json &j, DBPoolConfig &dbp)
    {
        dbp.db_type = dbTypeFromString(j["db_type"]);
        dbp.db_config = j["db_config"];
        dbp.max_pool_size = j["max_pool_size"];
        dbp.min_idle = j["min_idle"];
        dbp.get_connection_timeout_ms = j["get_connection_timeout_ms"];
        dbp.idle_timeout_seconds = j["idle_timeout_seconds"];
        dbp.max_life_time_seconds = j["max_life_time_seconds"];
        // 新增字段向后兼容: 旧配置缺失时使用默认值
        dbp.keepalive_seconds = j.value("keepalive_seconds", 60);
        dbp.borrowed_timeout_seconds = j.value("borrowed_timeout_seconds", 30);
        dbp.scan_interval_ms = j.value("scan_interval_ms", 1000);
    }

    DBPoolConfig DBPoolConfig::defaultConfig()
    {
        return DBPoolConfig{
            .db_type = DBType::MYSQL,
            .db_config = DBConfig{
                .host = "127.0.0.1",
                .port = 3306,
                .user = "csj",
                .password = "12345678",
                .dbname = "test_db"},
            .max_pool_size = 16,
            .min_idle = 16,
            .get_connection_timeout_ms = 300,
            .idle_timeout_seconds = 60,
            .max_life_time_seconds = 3600,
            .keepalive_seconds = 60,
            .borrowed_timeout_seconds = 30,
            .scan_interval_ms = 1000};
    }

} // namespace db

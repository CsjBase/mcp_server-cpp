#pragma once

#include <string>
#include <stdint.h>
#include <nlohmann/json.hpp>

namespace db
{
    using json = nlohmann::json;

    struct DBConfig
    {
        std::string host;
        uint16_t port;
        std::string user;
        std::string password;
        std::string dbname;

        bool operator==(const DBConfig &other)
        {
            return host == other.host &&
                   port == other.port &&
                   user == other.user &&
                   password == other.password &&
                   dbname == other.dbname;
        }
    };

    void to_json(json &j, const DBConfig &);
    void from_json(const json &j, DBConfig &);

}
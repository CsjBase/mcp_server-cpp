#include "DBConfig.h"

namespace db
{

    void to_json(json &j, const DBConfig &db)
    {
        j["host"] = db.host;
        j["port"] = db.port;
        j["user"] = db.user;
        j["password"] = db.password;
        j["dbname"] = db.dbname;
    }
    void from_json(const json &j, DBConfig &db)
    {
        db.host = j["host"].get<std::string>();
        db.port = j["port"].get<uint16_t>();
        db.user = j["user"].get<std::string>();
        db.password = j["password"].get<std::string>();
        db.dbname = j["dbname"].get<std::string>();
    }

}
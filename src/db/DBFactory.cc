#include "DBFactory.h"
#include "MySQL.h"

namespace db
{

    IDBFactory::ptr MySQLFactory::Create()
    {
        return std::make_shared<MySQLFactory>();
    }

    DBType MySQLFactory::type() const
    {
        return DBType::MYSQL;
    }

    IDB::ptr MySQLFactory::create(const DBConfig &config)
    {
        return std::make_shared<MySQL>(config);
    }

}
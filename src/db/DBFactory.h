#pragma once

#include "IDB.h"
#include "DBConfig.h"
#include "DBPoolConfig.h"

#include <map>
#include <string>
#include <memory>

namespace db
{

    class IDBFactory
    {
    public:
        typedef std::shared_ptr<IDBFactory> ptr;
        virtual ~IDBFactory() {}
        virtual DBType type() const = 0;

        virtual IDB::ptr create(const DBConfig &) = 0;
    };

    class MySQLFactory : public IDBFactory
    {
    public:
        static IDBFactory::ptr Create();
        DBType type() const override;

        IDB::ptr create(const DBConfig &config) override;
    };

}
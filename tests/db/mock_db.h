#pragma once

#include "db/IDB.h"
#include "db/DBFactory.h"
#include "db/DBConfig.h"
#include "db/DBPoolConfig.h"

#include <atomic>
#include <mutex>
#include <vector>
#include <thread>

using namespace db;

namespace test
{

    // 记录调用的 Mock IDB
    class MockDB : public IDB, public std::enable_shared_from_this<MockDB>
    {
    public:
        typedef std::shared_ptr<MockDB> ptr;

        explicit MockDB(const DBConfig &cfg) : m_config(cfg) {}

        // ---- ISQLUpdate ----
        int execute(const char *format, ...) override
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            ++m_execCount;
            return 0;
        }
        int execute(const std::string &sql) override
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            ++m_execCount;
            m_lastSql = sql;
            return 0;
        }
        uint64_t getAffectedRows() override { return 1; }
        int64_t getLastInsertId() override { return ++m_lastId; }

        // ---- ISQLQuery ----
        ISQLData::ptr query(const char *format, ...) override { return nullptr; }
        ISQLData::ptr query(const std::string &sql) override
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            ++m_queryCount;
            return nullptr;
        }

        // ---- IStmt ----
        IStmt::ptr prepare(const std::string &stmt) override { return nullptr; }

        // ---- IDB ----
        ITransaction::ptr openTransaction(bool auto_commit) override { return nullptr; }
        int getErrno() override { return 0; }
        std::string getErrStr() override { return ""; }

        bool connect() override
        {
            m_connected.store(true);
            return true;
        }
        bool isValid() override { return m_connected.load(); }
        bool ping() override
        {
            ++m_pingCount;
            return m_connected.load();
        }
        uint64_t getInsertId() override { return 0; }

        // ---- 测试辅助 ----
        int execCount() const { return m_execCount.load(); }
        int queryCount() const { return m_queryCount.load(); }
        int pingCount() const { return m_pingCount.load(); }
        const DBConfig &config() const { return m_config; }
        void setValid(bool v) { m_connected.store(v); }

    private:
        DBConfig m_config;
        std::atomic<int> m_execCount{0};
        std::atomic<int> m_queryCount{0};
        std::atomic<int> m_pingCount{0};
        std::atomic<int64_t> m_lastId{0};
        std::atomic<bool> m_connected{false};
        std::mutex m_mutex;
        std::string m_lastSql;
    };

    // Mock 工厂
    class MockDBFactory : public IDBFactory
    {
    public:
        DBType type() const override { return m_type; }
        IDB::ptr create(const DBConfig &cfg) override
        {
            auto db = std::make_shared<MockDB>(cfg);
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                m_created.push_back(db);
            }
            return db;
        }

        void setType(DBType t) { m_type = t; }
        int createdCount() const
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            return (int)m_created.size();
        }
        std::vector<MockDB::ptr> created() const
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            return m_created;
        }

    private:
        DBType m_type{DBType::UNKNOWN};
        mutable std::mutex m_mutex;
        std::vector<MockDB::ptr> m_created;
    };

} // namespace test
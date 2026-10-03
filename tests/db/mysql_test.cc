#include <gtest/gtest.h>
#include <thread>
#include "db/MySQL.h"
#include "db/DBConfig.h"
// #include "mysql_config_helper.h"   // 见下方辅助

using namespace db;

class MySQLTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        DBConfig config{
            .host = "127.0.0.1",
            .port = 3306,
            .user = "csj",
            .password = "12345678",
            .dbname = "test_db"};
        m_db = std::make_shared<MySQL>(config);
        ASSERT_TRUE(m_db->connect());
        m_db->execute("CREATE TABLE IF NOT EXISTS t_user ("
                      "    id      BIGINT PRIMARY KEY AUTO_INCREMENT,"
                      "    name    VARCHAR(64) NOT NULL,"
                      "    age     INT DEFAULT 0,"
                      "    score   DOUBLE NOT NULL DEFAULT 0,"
                      "    created DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
                      "    UNIQUE KEY uk_name (name)"
                      ") ENGINE=InnoDB;");
        m_db->execute("TRUNCATE TABLE t_user");
    }

    MySQL::ptr m_db;
};

DBConfig g_cfg{
    .host = "127.0.0.1",
    .port = 3306,
    .user = "csj",
    .password = "12345678",
    .dbname = "test_db"};

TEST_F(MySQLTest, ConnectAndPing)
{
    EXPECT_TRUE(m_db->isValid());
    EXPECT_TRUE(m_db->ping());
}

TEST_F(MySQLTest, InsertSelect)
{
    ASSERT_EQ(m_db->execute("INSERT INTO t_user(name,age,score) VALUES('alice',20,88.5)"), 0);
    auto id = m_db->getLastInsertId();
    EXPECT_GT(id, 0);

    auto res = m_db->query("SELECT name,age,score FROM t_user WHERE id=%ld", id);
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->next());
    EXPECT_EQ(res->getString(0), "alice");
    EXPECT_EQ(res->getInt32(1), 20);
    EXPECT_DOUBLE_EQ(res->getDouble(2), 88.5);
    EXPECT_FALSE(res->next());
}

TEST_F(MySQLTest, QueryWithFormatInjectionSafeViaStmt)
{
    // 预处理防注入
    auto stmt = m_db->prepare("INSERT INTO t_user(name,age) VALUES(?,?)");
    ASSERT_NE(stmt, nullptr);
    stmt->bindString(1, std::string("bob'; DROP TABLE t_user; --"));
    stmt->bindInt32(2, 25);
    EXPECT_EQ(stmt->execute(), 0);

    auto res = m_db->query("SELECT COUNT(*) FROM t_user");
    ASSERT_TRUE(res && res->next());
    EXPECT_EQ(res->getInt32(0), 1);
}

TEST_F(MySQLTest, TransactionCommit)
{
    auto txn = m_db->openTransaction(true);
    ASSERT_NE(txn, nullptr);
    ASSERT_EQ(txn->execute("INSERT INTO t_user(name,age) VALUES('c1',1)"), 0);
    ASSERT_TRUE(txn->commit());

    auto res = m_db->query("SELECT COUNT(*) FROM t_user WHERE name='c1'");
    ASSERT_TRUE(res && res->next());
    EXPECT_EQ(res->getInt32(0), 1);
}

TEST_F(MySQLTest, TransactionRollback)
{
    {
        auto txn = m_db->openTransaction(false);
        ASSERT_EQ(txn->execute("INSERT INTO t_user(name,age) VALUES('r1',1)"), 0);
        EXPECT_TRUE(txn->rollback());
    }
    auto res = m_db->query("SELECT COUNT(*) FROM t_user WHERE name='r1'");
    ASSERT_TRUE(res && res->next());
    EXPECT_EQ(res->getInt32(0), 0);
}

TEST_F(MySQLTest, StmtQueryTypes)
{
    m_db->execute("INSERT INTO t_user(name,age,score,created) VALUES('t',7,1.5,NOW())");
    auto stmt = m_db->prepare("SELECT id,name,age,score,created FROM t_user LIMIT 1");
    ASSERT_NE(stmt, nullptr);
    auto res = stmt->query();
    ASSERT_NE(res, nullptr);
    ASSERT_TRUE(res->next());
    EXPECT_GT(res->getInt64(0), 0);
    EXPECT_EQ(res->getString(1), "t");
    EXPECT_EQ(res->getInt32(2), 7);
    EXPECT_DOUBLE_EQ(res->getDouble(3), 1.5);
    EXPECT_GT(res->getTime(4), 0);
    EXPECT_EQ(res->getColumnName(1), "name");
    EXPECT_EQ(res->getColumnType(2), ColumnType::LONG);
}

TEST_F(MySQLTest, BindNull)
{
    auto stmt = m_db->prepare("INSERT INTO t_user(name,age) VALUES(?,?)");
    stmt->bindString(1, std::string("null_user"));
    stmt->bindNull(2);
    EXPECT_EQ(stmt->execute(), 0);
    std::cout << stmt->getErrStr() << std::endl;
}

TEST_F(MySQLTest, InvalidSqlSetsError)
{
    EXPECT_NE(m_db->execute("THIS IS NOT SQL"), 0);
    EXPECT_FALSE(m_db->getErrStr().empty());
    EXPECT_NE(m_db->getErrno(), 0);
}

TEST_F(MySQLTest, ReconnectAfterClose)
{
    // 强制断开
    m_db->getRaw().reset();
    EXPECT_TRUE(m_db->connect());
    EXPECT_TRUE(m_db->isValid());
}

TEST_F(MySQLTest, ConcurrentStmtBinding)
{
    // 多线程各自 stmt，验证 buffer 独立
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i)
    {
        ts.emplace_back([this, i]
                        {
            auto db = std::make_shared<MySQL>(g_cfg);
            ASSERT_TRUE(db->connect());
            for (int j = 0; j < 50; ++j)
            {
                auto stmt = db->prepare("INSERT INTO t_user(name,age) VALUES(?,?)");
                std::string name = "u_" + std::to_string(i) + "_" + std::to_string(j);
                stmt->bindString(1, name);
                stmt->bindInt32(2, i * 100 + j);
                ASSERT_EQ(stmt->execute(), 0);
            } });
    }
    for (auto &t : ts)
        t.join();

    auto res = m_db->query("SELECT COUNT(*) FROM t_user");
    ASSERT_TRUE(res && res->next());
    EXPECT_EQ(res->getInt32(0), 400);
}
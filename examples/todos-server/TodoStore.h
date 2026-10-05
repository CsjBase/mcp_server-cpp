#pragma once

#include "db/DBPool.h"

#include <nlohmann/json.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <atomic>

namespace todos
{

    using json = nlohmann::json;

    struct Todo
    {
        int64_t id;
        std::string title;
        bool completed = false;
        int64_t created_at = 0;

        json to_json() const
        {
            return {
                {"id", id},
                {"title", title},
                {"completed", completed},
                {"createdAt", created_at}};
        }
    };

    // 基于数据库的待办存储。
    //
    // 与内存版的区别：
    //   - 数据持久化在数据库表中
    //   - 所有写操作通过预处理语句（IStmt）执行，防 SQL 注入
    //   - 读操作返回结果集，逐行映射为 Todo 结构
    class TodoStore
    {
    public:
        explicit TodoStore()
        {
        }

        // 建表。应在服务启动时调用一次。
        // 使用 IF NOT EXISTS 保证幂等。
        void init_schema()
        {
            auto conn = db::DBPool::instance().getConnection();
            int rc = conn->execute(
                "CREATE TABLE IF NOT EXISTS todos ("
                "  id         BIGINT       AUTO_INCREMENT PRIMARY KEY,"
                "  title      VARCHAR(200) NOT NULL,"
                "  completed  TINYINT      NOT NULL DEFAULT 0,"
                "  created_at BIGINT       NOT NULL,"
                "  INDEX idx_created_at (created_at)"
                ")");
            if (rc != 0)
            {
                throw std::runtime_error(
                    "Failed to create todos table: " + conn->getErrStr());
            }
        }

        // ---- 创建 ----
        Todo create(const std::string &title)
        {
            auto conn = db::DBPool::instance().getConnection();
            // 预处理语句：防注入
            auto stmt = conn->prepare(
                "INSERT INTO todos (title, completed, created_at) "
                "VALUES (?, ?, ?)");
            if (!stmt)
            {
                throw std::runtime_error(
                    "Failed to prepare INSERT: " + conn->getErrStr());
            }

            Todo t;
            t.title = title;
            t.completed = false;
            t.created_at = now_seconds();

            stmt->bindString(1, t.title);
            stmt->bindInt8(2, 0);
            stmt->bindInt64(3, t.created_at);

            if (stmt->execute() != 0)
            {
                throw std::runtime_error(
                    "Failed to insert todo: " + stmt->getErrStr());
            }
            t.id = stmt->getLastInsertId();

            return t;
        }

        // ---- 查询单个 ----
        std::optional<Todo> get(uint64_t id)
        {
            auto conn = db::DBPool::instance().getConnection();
            auto stmt = conn->prepare(
                "SELECT id, title, completed, created_at "
                "FROM todos WHERE id = ?");
            if (!stmt)
            {
                throw std::runtime_error(
                    "Failed to prepare SELECT: " + conn->getErrStr());
            }
            stmt->bindInt64(1, id);

            auto data = stmt->query();
            if (!data || data->getErrno() != 0)
            {
                throw std::runtime_error(
                    "Failed to query todo: " +
                    (data ? data->getErrStr() : stmt->getErrStr()));
            }

            if (!data->next())
            {
                return std::nullopt;
            }
            return row_to_todo(*data);
        }

        // ---- 标记完成 ----
        // 返回 false 表示 id 不存在（而非执行失败）。
        // 执行失败会抛异常。
        bool complete(int64_t id)
        {
            auto conn = db::DBPool::instance().getConnection();
            auto transaction = conn->openTransaction();
            if (!transaction)
            {
                throw std::runtime_error("Failed to open transaction");
            }
            auto stmt = transaction->prepare("UPDATE todos SET completed = 1 WHERE id = ?");
            if (!stmt)
            {
                throw std::runtime_error(
                    "Failed to prepare UPDATE: " + conn->getErrStr());
            }
            stmt->bindInt64(1, id);
            if (stmt->execute() != 0)
            {
                throw std::runtime_error(
                    "Failed to complete todo: " + stmt->getErrStr());
            }
            bool res = transaction->getAffectedRows() > 0;
            transaction->commit();
            return res;
        }

        // ---- 删除 ----
        bool remove(int64_t id)
        {
            auto conn = db::DBPool::instance().getConnection();
            auto transaction = conn->openTransaction();
            if (!transaction)
            {
                throw std::runtime_error("Failed to open transaction");
            }
            auto stmt = transaction->prepare("DELETE FROM todos WHERE id = ?");
            if (!stmt)
            {
                throw std::runtime_error("Failed to prepare statement");
            }
            stmt->bindInt64(1, id);
            if (stmt->execute() != 0)
            {
                throw std::runtime_error(
                    "Failed to delete todo: " + stmt->getErrStr());
            }
            bool res = transaction->getAffectedRows();
            transaction->commit();
            return res;
        }

        // ---- 列表 ----
        std::vector<Todo> list()
        {
            auto conn = db::DBPool::instance().getConnection();
            auto data = conn->query(
                "SELECT id, title, completed, created_at "
                "FROM todos ORDER BY created_at ASC, id ASC");
            if (!data || data->getErrno() != 0)
            {
                throw std::runtime_error(
                    "Failed to list todos: " +
                    (data ? data->getErrStr() : conn->getErrStr()));
            }

            std::vector<Todo> result;
            while (data->next())
            {
                result.push_back(row_to_todo(*data));
            }
            return result;
        }

        // ---- 按完成状态过滤 ----
        std::vector<Todo> list_by_status(bool completed)
        {
            auto conn = db::DBPool::instance().getConnection();
            auto stmt = conn->prepare(
                "SELECT id, title, completed, created_at "
                "FROM todos WHERE completed = ? "
                "ORDER BY created_at ASC, id ASC");
            if (!stmt)
            {
                throw std::runtime_error(
                    "Failed to prepare list_by_status: " + conn->getErrStr());
            }
            stmt->bindInt8(1, completed ? 1 : 0);

            auto data = stmt->query();
            if (!data || data->getErrno() != 0)
            {
                throw std::runtime_error(
                    "Failed to query by status: " +
                    (data ? data->getErrStr() : stmt->getErrStr()));
            }

            std::vector<Todo> result;
            while (data->next())
            {
                result.push_back(row_to_todo(*data));
            }
            return result;
        }

    private:
        // ---- 行映射 ----
        //
        // ISQLData 的列索引从 0 开始，与 SELECT 中的列顺序对应。
        // 用列名查找更稳健，但索引更快。这里用索引，
        // 并在每次 SELECT 时保持列顺序一致。
        static Todo row_to_todo(db::ISQLData &data)
        {
            Todo t;
            t.id = data.getInt64(0);
            t.title = data.getString(1);
            t.completed = (data.getInt8(2) != 0);
            t.created_at = data.getInt64(3);
            return t;
        }

        static int64_t now_seconds()
        {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        }
    };

} // namespace todos
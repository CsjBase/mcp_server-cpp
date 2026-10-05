# db — MySQL 数据库模块

基于官方 libmysql（`unofficial-libmysql`）的数据库抽象层：统一接口（`IDB`）+ MySQL 实现（`MySQL`）+ **借/还完全无锁的连接池**（`DBPool`）。接口设计参考 sylar 风格，覆盖查询、更新、预处理语句与事务。

## 目录结构

```
db/
├── IDB.h            # 核心接口：IDB / ISQLData / ISQLUpdate / ISQLQuery / IStmt / ITransaction
├── MySQL.h / .cc    # MySQL 实现（libmysql）
├── DBConfig.h       # 连接参数（host/port/user/password/dbname）+ JSON 序列化
├── DBPoolConfig.h   # 连接池参数
├── DBFactory.h      # 工厂接口 IDBFactory + MySQLFactory（测试可注入 Mock）
├── DBPool.h / .cc   # 无锁连接池（单例）
└── Log.h            # 模块内日志别名
```

## 核心接口

```
IDB ─────────────── 连接主接口
├── ISQLUpdate     execute(sql) → 受影响行数 / last_insert_id
├── ISQLQuery      query(sql)   → ISQLData 结果集
├── prepare(stmt)  → IStmt    预处理语句（防注入、可重复执行）
└── openTransaction(auto_commit) → ITransaction 事务

ISQLData          查询结果集（行游标 next() + 按列类型取值）
ITransaction      begin / commit / rollback + prepare + 查询/更新
```

## 快速上手

```cpp
#include "db/DBPool.h"
#include "config/Config.h"

// 1. 配置连接池（单例在首次调用时读取 config 模块的 "dbpool" 配置项）
//    conf/db.json 示例：
//    {
//      "dbpool": {
//        "db_type": "mysql",
//        "db_config": {"host": "127.0.0.1", "port": 3306,
//                      "user": "root", "password": "password", "dbname": "demo"},
//        "max_pool_size": 20,
//        "min_idle": 5,
//        "get_connection_timeout_ms": 3000
//      }
//    }
config::Config::loadFromFile("conf/db.json");

// 2. 借连接（RAII：归还时自动回池）
auto conn = db::DBPool::instance().getConnection();
if (!conn) {
    // 池空且等待超时
}

// 1. 查询
auto data = conn->query("SELECT id, title FROM todos WHERE id > %d", 10);
while (data->next()) {
    int64_t id = data->getInt64(0);
    std::string title = data->getString(1);
}

// 2. 更新（仅用于受信 SQL）
conn->execute("UPDATE todos SET completed = 1 WHERE id = %d", 42);
uint64_t affected = conn->getAffectedRows();

// 3. 预处理语句（防 SQL 注入，推荐用于外部输入）
auto stmt = conn->prepare("INSERT INTO todos (title, completed) VALUES (?, ?)");
stmt->bindString(1, title);
stmt->bindInt8(2, 0);
stmt->execute();
int64_t newId = stmt->getLastInsertId();

// 4. 事务
auto tx = conn->openTransaction(/*auto_commit=*/false);
tx->begin();
tx->execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1");
tx->execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2");
tx->commit();   // 失败时 rollback()
```

### ISQLData 取值一览

`getInt8/16/32/64`、`getUint8/16/32/64`、`getFloat`、`getDouble`、`getString`、`getBlob`、`getTime`，另有 `isNull(idx)`、`getColumnCount()`、`getColumnName(idx)`、`getColumnType(idx)`（统一 `ColumnType` 枚举避免各实现语义不一致）。

## 连接池设计（无锁借还）

```
业务线程                    维护线程（每 scanIntervalMs 扫描）
   │                              │
   │  getConnection()             │
   ├─ 无锁 dequeue 空闲队列 ───────┤ 无锁 drain 全部空闲连接，独占处理：
   │   （mpmc_bounded_queue）     │   ├─ keepalive ping
   │                              │   ├─ idleTimeout / maxLifetime 淘汰
   │  池空 → 等待协议：            │   ├─ 补充到 minIdle（异步创建线程池）
   │   waiterCount_ 原子快速路径   │   └─ 借出超时（泄漏）检测
   │   + cv 等待（仅此时加锁）     │
   └─ 归还：检查有效性（锁外）     │
       无锁 enqueue 回池           │
```

- **借/还零互斥**：空闲连接存在有界无锁 MPMC 队列（[utils/mpmc_bounded_queue.h](../utils/mpmc_bounded_queue.h)）中，借出 = 无锁 dequeue，归还 = 无锁 enqueue。
- **时间戳走后台时钟缓存**：借出时间、活跃时间与超时判定统一使用 [utils/cached_clock](../utils/README.md)（~1ms 粒度原子单调钟）——阈值均为秒级，误差 <0.1%，同时规避 `clock_gettime` 在虚拟化环境的高开销。
- **池空等待协议**：`waiterCount_` 原子量做快速路径判断，只有确实存在等待者时才触碰 mutex/cv，空池以外零锁竞争。
- **维护线程**：每秒将空闲队列一次性无锁排空，独占执行保活（ping）、超时剔除（空闲超时/最大生命周期）、按需补充（异步创建线程池）、借出泄漏检测（borrowedTimeout），全程不阻塞业务线程。
- **连接状态机**：`Creating → Idle ⇄ InUse → Broken`，`state_` 为原子量，借出/归还/维护线程跨线程可见性均依赖它。
- **连接注册表**：创建/销毁时登记，泄漏检测遍历用（借出超时的连接会被标记并强制回收）。

### 使用约束

- 连接借出后**请勿跨线程传递**（`shared_ptr` 保证归还调用安全，但底层 MySQL 连接本身非线程安全）。
- 连接池析构前，所有借出的连接必须已归还。
- `execute(const char* format, ...)` 仅用于受信 SQL，不防注入；外部输入必须走 `IStmt` 绑定参数。

### 配置项（DBPoolConfig）

| 项 | 默认 | 说明 |
|---|---|---|
| `max_pool_size` | — | 最大连接数 |
| `min_idle` | — | 最小空闲连接数（维护线程自动补充） |
| `get_connection_timeout_ms` | — | 池空时借出等待超时 |
| `idle_timeout_seconds` | — | 空闲超时剔除（从上次使用起算） |
| `max_life_time_seconds` | — | 最大生命周期（从创建起算） |
| `keepalive_seconds` | 60 | 空闲连接保活 ping 间隔 |
| `borrowed_timeout_seconds` | 30 | 借出泄漏检测时长 |
| `scan_interval_ms` | 1000 | 维护扫描间隔 |

## 测试

单元测试位于 [tests/db](../../tests/db/)；`DBPool` 支持注入 `IDBFactory`（见 [mock_db.h](../../tests/db/mock_db.h)），池的借还/超时/泄漏检测逻辑无需真实 MySQL 即可测试。

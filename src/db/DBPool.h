#pragma once

#include "IDB.h"
#include "DBFactory.h"
#include "DBPoolConfig.h"
#include "utils/thread_pool.h"
#include "utils/mpmc_bounded_queue.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace db
{

    using Timestamp = std::chrono::steady_clock::time_point;

    /// 连接状态机: Creating → Idle ⇄ InUse → Broken
    /// state_ 为原子量: 借出/归还/维护线程跨线程可见性依赖它
    enum class ConnState
    {
        Creating,  // 创建中
        Idle,      // 空闲可用(在空闲队列中)
        InUse,     // 已借出
        Keepalive, // 维护线程正在 ping
        Broken,    // 已损坏, 待销毁
        Closing,   // 关闭中(保留)
        Closed,    // 已关闭(保留)
    };

    class DBConnection : public IDB
    {
    public:
        typedef std::shared_ptr<DBConnection> ptr;

        explicit DBConnection(IDB::ptr conn);
        ~DBConnection();

        // ---- IDB 接口: 全部委托底层连接 ----
        int execute(const char *format, ...) override;
        int execute(const std::string &sql) override;
        uint64_t getAffectedRows() override;
        int64_t getLastInsertId() override;
        ISQLData::ptr query(const char *format, ...) override;
        ISQLData::ptr query(const std::string &sql) override;

        IStmt::ptr prepare(const std::string &stmt) override;
        int getErrno() override;
        std::string getErrStr() override;
        ITransaction::ptr openTransaction(bool auto_commit = false) override;

        bool connect() override;
        bool isValid() override;
        bool ping() override;
        uint64_t getInsertId() override;

        // ---- 超时判定(仅在独占该连接时调用) ----
        bool isIdleTimeout(int seconds);
        bool isLifeTimeout(int seconds);
        bool isKeepaliveTimeout(int seconds);
        bool isBorrowedTimeout(int seconds);

        void refreshLastUsedTime();
        Timestamp getLastUsedTime() const { return lastUsedAt_; }
        Timestamp getBorrowedTime() const { return borrowedAt_.load(std::memory_order_acquire); }

        // 状态与借出时间: 原子量, 供维护线程泄漏检测跨线程读取
        void setState(ConnState state) { state_.store(state, std::memory_order_release); }
        ConnState getState() const { return state_.load(std::memory_order_acquire); }
        void setBorrowedTime(Timestamp t) { borrowedAt_.store(t, std::memory_order_release); }

        IDB::ptr getRaw() const { return conn_; }

    private:
        IDB::ptr conn_;
        std::atomic<ConnState> state_{ConnState::Creating};
        Timestamp createdAt_;  // 创建时间, 用于 maxLifetime
        Timestamp lastUsedAt_; // 上次使用/保活时间, 用于 idleTimeout 和 keepalive
        std::atomic<Timestamp> borrowedAt_{}; // 借出时间, 用于泄漏检测(跨线程读)
    };

    /**
     * @brief 数据库连接池: 借/还完全无锁
     *
     * 锁竞争优化设计:
     * - 空闲连接存放在有界无锁 MPMC 队列(mpmc_bounded_queue)中,
     *   借出 = 无锁 dequeue, 归还 = 无锁 enqueue, 业务线程零互斥
     * - 只有池空时借出才进入等待协议: waiterCount_ 原子快速路径,
     *   仅当存在等待者时才触碰 waitMutex_/cv
     * - 维护线程每秒将空闲队列一次性无锁排空(drain), 独占处理
     *   保活/超时剔除/补充, 全程不阻塞业务线程
     * - 归还时的有效性/生命周期检查在锁外完成(连接被归还者独占)
     * - 所有计数(总数/空闲数/补充标记)均为原子量
     *
     * 约束: 连接借出后请勿跨线程传递(shared_ptr 保证归还调用安全,
     * 但底层 MySQL 连接本身非线程安全); 析构前所有借出的连接必须已归还。
     */
    class DBPool
    {
    public:
        static DBPool &instance();

        /// 直接构造(测试可注入 Mock 工厂)
        DBPool(IDBFactory::ptr factory, const DBPoolConfig &config);

        DBPool(const DBPool &) = delete;
        DBPool &operator=(const DBPool &) = delete;
        ~DBPool();

        /// 借出一个连接, 归还时自动回池; 池空等待超时返回 nullptr
        DBConnection::ptr getConnection();

        /// 池状态快照(近似值, 供监控/测试)
        struct Stats
        {
            int total = 0;  // 已创建 + 创建中
            int idle = 0;   // 空闲
            int inUse = 0;  // 借出中
            int maxSize = 0;
            int minIdle = 0;
        };
        Stats stats() const;

    private:
        void init();
        void shutdown();

        void releaseConnection(DBConnection *dbconn);
        void maintenanceLoop();
        void scanConnections();
        void scanBorrowedLeaks();
        void replenishToMinIdle();
        void createOne();
        void discard(DBConnection *conn);

        /// 无锁取出一个空闲连接并标记 InUse; 队列空返回 nullptr
        DBConnection *takeIdle();
        /// 池空等待协议(带超时); 超时/停止返回 nullptr
        DBConnection *waitForIdle();
        void notifyMaintenance();

        IDBFactory::ptr factory_;
        DBConfig dbconfig_;

        int maxSize_ = 20;             // 最大连接数
        int minIdle_ = 5;              // 最小空闲连接数
        int connectionTimeoutMs_ = 3000; // 获取连接超时时长
        int idleTimeoutSec_ = 300;     // 空闲连接超时时长
        int maxLifetimeSec_ = 1800;    // 连接最大生命周期
        int keepaliveSec_ = 60;        // 空闲连接触发keepalive检测时长
        int borrowedTimeoutSec_ = 30;  // 连接泄漏检测时长
        int scanIntervalMs_ = 1000;    // 维护扫描间隔

        std::atomic<int> totalConnCount_{0}; // 已创建 + 创建中的连接总数
        std::atomic<int> idleCount_{0};      // 空闲队列中的连接数(近似)
        std::atomic<bool> needReplenish_{false};
        std::atomic<bool> stopped_{false};

        utils::mpmc_bounded_queue<DBConnection *> idleQueue_; // 无锁空闲队列

        // ---- 池空等待协议(仅池空时使用) ----
        std::atomic<int> waiterCount_{0};
        std::mutex waitMutex_;
        std::condition_variable cvIdleAvailable_;

        // ---- 维护线程 ----
        std::mutex maintenanceMutex_;
        std::condition_variable cvMaintenance_;

        // ---- 连接注册表(创建/销毁时更新, 泄漏检测遍历) ----
        std::mutex allConnsMutex_;
        std::vector<DBConnection *> allConns_;

        std::shared_ptr<utils::ThreadPool> createPool_; // 异步创建连接线程池
        std::thread maintenanceThread_;                 // 维护线程
    };

} // namespace db

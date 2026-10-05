#include "DBPool.h"
#include "Log.h"
#include "config/Config.h"
#include "utils/cached_clock.h"
#include <stdarg.h>
#include <algorithm>
#include <stdexcept>

namespace db
{
    config::ConfigVar<DBPoolConfig>::ptr g_dbpool_config =
        config::Config::lookup("dbpool", DBPoolConfig::defaultConfig(), "dbpool config");

    // ---------------- DBConnection ----------------

    DBConnection::DBConnection(IDB::ptr conn)
        : conn_(conn),
          createdAt_(utils::cached_steady_now()),
          lastUsedAt_(utils::cached_steady_now())
    {
    }

    DBConnection::~DBConnection() {}

    // 安全 vasprintf 包装
    static std::string safeVFormat(const char *format, va_list ap)
    {
        char *buf = nullptr;
        int len = vasprintf(&buf, format, ap);
        if (len < 0 || buf == nullptr)
        {
            if (buf)
                free(buf);
            return "";
        }
        std::string ret(buf, len);
        free(buf);
        return ret;
    }

    int DBConnection::execute(const char *format, ...)
    {
        va_list ap;
        va_start(ap, format);
        std::string sql = safeVFormat(format, ap);
        va_end(ap);
        if (sql.empty() && format && *format)
            return -1;
        return conn_->execute(sql);
    }

    int DBConnection::execute(const std::string &sql) { return conn_->execute(sql); }
    uint64_t DBConnection::getAffectedRows() { return conn_->getAffectedRows(); }
    int64_t DBConnection::getLastInsertId() { return conn_->getLastInsertId(); }

    ISQLData::ptr DBConnection::query(const char *format, ...)
    {
        va_list ap;
        va_start(ap, format);
        std::string sql = safeVFormat(format, ap);
        va_end(ap);
        if (sql.empty() && format && *format)
            return nullptr;
        return conn_->query(sql);
    }

    ISQLData::ptr DBConnection::query(const std::string &sql) { return conn_->query(sql); }

    IStmt::ptr DBConnection::prepare(const std::string &stmt) { return conn_->prepare(stmt); }
    int DBConnection::getErrno() { return conn_->getErrno(); }
    std::string DBConnection::getErrStr() { return conn_->getErrStr(); }
    ITransaction::ptr DBConnection::openTransaction(bool auto_commit) { return conn_->openTransaction(auto_commit); }
    bool DBConnection::connect() { return conn_->connect(); }
    bool DBConnection::isValid() { return conn_->isValid(); }
    bool DBConnection::ping() { return conn_->ping(); }
    uint64_t DBConnection::getInsertId() { return conn_->getInsertId(); }

    // 超时判定统一用缓存单调钟(~1ms 粒度): 阈值均为秒级, 误差 <0.1%,
    // 且规避 clock_gettime 在虚拟化环境下的高开销(VM 拦截实测 ~20us/次)
    bool DBConnection::isIdleTimeout(int seconds)
    {
        auto now = utils::cached_steady_now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - lastUsedAt_).count() >= seconds;
    }

    bool DBConnection::isLifeTimeout(int seconds)
    {
        auto now = utils::cached_steady_now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - createdAt_).count() >= seconds;
    }

    bool DBConnection::isKeepaliveTimeout(int seconds)
    {
        auto now = utils::cached_steady_now();
        return std::chrono::duration_cast<std::chrono::seconds>(now - lastUsedAt_).count() >= seconds;
    }

    bool DBConnection::isBorrowedTimeout(int seconds)
    {
        auto now = utils::cached_steady_now();
        return std::chrono::duration_cast<std::chrono::seconds>(
                   now - borrowedAt_.load(std::memory_order_acquire))
                   .count() >= seconds;
    }

    void DBConnection::refreshLastUsedTime()
    {
        lastUsedAt_ = utils::cached_steady_now();
    }

    // ---------------- DBPool ----------------

    DBPool &DBPool::instance()
    {
        static DBPool instance(MySQLFactory::Create(), g_dbpool_config->getValue());
        return instance;
    }

    DBPool::DBPool(IDBFactory::ptr factory, const DBPoolConfig &config)
        : factory_(std::move(factory)),
          dbconfig_(config.db_config),
          maxSize_(std::max(1, config.max_pool_size)),
          minIdle_(std::min(std::max(0, config.min_idle), maxSize_)),
          connectionTimeoutMs_(std::max(1, config.get_connection_timeout_ms)),
          idleTimeoutSec_(std::max(0, config.idle_timeout_seconds)),
          maxLifetimeSec_(std::max(1, config.max_life_time_seconds)),
          keepaliveSec_(std::max(1, config.keepalive_seconds)),
          borrowedTimeoutSec_(std::max(1, config.borrowed_timeout_seconds)),
          scanIntervalMs_(std::max(10, config.scan_interval_ms)),
          idleQueue_(std::max(1, config.max_pool_size)),
          createPool_(std::make_shared<utils::ThreadPool>(2, 1024))
    {
        init();
    }

    DBPool::~DBPool()
    {
        shutdown();
    }

    void DBPool::init()
    {
        // 先建初始连接
        try
        {
            for (int i = 0; i < minIdle_; ++i)
            {
                IDB::ptr conn = factory_->create(dbconfig_);
                if (!conn || !conn->connect())
                    throw std::runtime_error("DBPool::init() create connection failed.");
                DBConnection *wrap = new DBConnection(conn);
                {
                    std::lock_guard<std::mutex> lock(allConnsMutex_);
                    allConns_.push_back(wrap);
                }
                totalConnCount_.fetch_add(1, std::memory_order_relaxed);
                wrap->setState(ConnState::Idle);
                idleQueue_.enqueue(wrap);
                idleCount_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        catch (...)
        {
            // 构造失败: 清理已创建的连接后抛出(对象不会完成构造, 队列中残留的
            // 悬垂指针不会再被访问)
            DB_LOG_ERROR("DBPool::init() create initial connections failed");
            for (auto *c : allConns_)
                delete c;
            allConns_.clear();
            stopped_.store(true, std::memory_order_release);
            throw;
        }

        maintenanceThread_ = std::thread(&DBPool::maintenanceLoop, this);
    }

    void DBPool::shutdown()
    {
        if (stopped_.exchange(true, std::memory_order_acq_rel))
            return;
        {
            std::lock_guard<std::mutex> lock(waitMutex_);
            cvIdleAvailable_.notify_all(); // 唤醒等待者, 使其观察到 stopped_ 后返回
        }
        {
            std::lock_guard<std::mutex> lock(maintenanceMutex_);
            cvMaintenance_.notify_all();
        }

        maintenanceThread_.join();
        createPool_->stop_gracefully(); // 等待在途的异步创建任务完成

        // 排空空闲队列(连接统一按注册表释放)
        DBConnection *c = nullptr;
        while (idleQueue_.dequeue(c))
        {
        }

        std::vector<DBConnection *> all;
        {
            std::lock_guard<std::mutex> lock(allConnsMutex_);
            all.swap(allConns_);
        }
        for (auto *conn : all)
            delete conn;
    }

    DBConnection::ptr DBPool::getConnection()
    {
        DBConnection *conn = takeIdle(); // 无锁快速路径
        if (!conn)
        {
            conn = waitForIdle(); // 池空: 等待协议(带超时)
            if (!conn)
            {
                DB_LOG_ERROR("DBPool::getConnection() timeout, pool exhausted");
                return nullptr;
            }
        }
        // 空闲低于水位 → 触发补充
        if (idleCount_.load(std::memory_order_relaxed) < minIdle_ &&
            !needReplenish_.exchange(true, std::memory_order_acq_rel))
            notifyMaintenance();
        return DBConnection::ptr(conn, [this](DBConnection *c)
                                 { releaseConnection(c); });
    }

    DBConnection *DBPool::takeIdle()
    {
        DBConnection *conn = nullptr;
        if (idleQueue_.dequeue(conn))
        {
            idleCount_.fetch_sub(1, std::memory_order_relaxed);
            conn->setState(ConnState::InUse);
            conn->setBorrowedTime(utils::cached_steady_now());
        }
        return conn;
    }

    /**
     * 池空等待协议(仅当无锁出队失败时进入):
     * 1. waiterCount_++ 后再加锁双重检查, 保证归还方的通知不丢失
     *    (归还方先无锁入队, 再检查 waiterCount_>0 才加锁通知);
     * 2. 因此连接充足时本路径零互斥, 仅池被耗尽时才触碰 waitMutex_。
     */
    DBConnection *DBPool::waitForIdle()
    {
        waiterCount_.fetch_add(1, std::memory_order_acq_rel);
        DBConnection *conn = nullptr;
        {
            std::unique_lock<std::mutex> lock(waitMutex_);
            auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(connectionTimeoutMs_);
            for (;;)
            {
                if ((conn = takeIdle()) != nullptr || stopped_.load(std::memory_order_acquire))
                    break;
                // 直到超时才放弃: 虚假唤醒/被其他等待者抢走连接时继续等待
                if (cvIdleAvailable_.wait_until(lock, deadline) == std::cv_status::timeout)
                    break;
            }
        }
        waiterCount_.fetch_sub(1, std::memory_order_acq_rel);
        return conn;
    }

    void DBPool::releaseConnection(DBConnection *conn)
    {
        if (!conn)
            return;
        // 归还检查: 连接被归还者独占, 全程无锁
        if (!conn->isValid() || conn->isLifeTimeout(maxLifetimeSec_))
        {
            discard(conn);
        }
        else
        {
            conn->refreshLastUsedTime();
            conn->setState(ConnState::Idle);
            // 无锁入队(队列容量≥最大连接数, 理论上不会满; 防御性自旋)
            int spins = 0;
            while (!idleQueue_.enqueue(conn))
            {
                if (++spins % 1000 == 0)
                    DB_LOG_ERROR("DBPool::releaseConnection() idle queue full, spinning...");
                std::this_thread::yield();
            }
            idleCount_.fetch_add(1, std::memory_order_relaxed);
            // 有等待者才加锁通知
            if (waiterCount_.load(std::memory_order_acquire) > 0)
            {
                std::lock_guard<std::mutex> lock(waitMutex_);
                cvIdleAvailable_.notify_all();
            }
        }
        // 空闲低于水位 → 触发补充
        if (idleCount_.load(std::memory_order_relaxed) < minIdle_ &&
            !needReplenish_.exchange(true, std::memory_order_acq_rel))
            notifyMaintenance();
    }

    void DBPool::maintenanceLoop()
    {
        while (!stopped_.load(std::memory_order_acquire))
        {
            {
                std::unique_lock<std::mutex> lock(maintenanceMutex_);
                cvMaintenance_.wait_for(lock, std::chrono::milliseconds(scanIntervalMs_), [this]()
                                        { return stopped_.load(std::memory_order_acquire) ||
                                                 needReplenish_.load(std::memory_order_acquire); });
                if (stopped_.load(std::memory_order_acquire))
                    return;
            }
            needReplenish_.store(false, std::memory_order_relaxed);

            scanConnections();
            scanBorrowedLeaks();

            if (needReplenish_.load(std::memory_order_relaxed) ||
                idleCount_.load(std::memory_order_relaxed) < minIdle_)
                replenishToMinIdle();
        }
    }

    /**
     * 维护扫描: 一次性无锁排空空闲队列(业务线程借/还完全不受影响),
     * 由维护线程独占这些连接完成保活/剔除, 处理完再回队。
     * 所有网络 I/O(ping)与日志均在无锁状态下进行。
     */
    void DBPool::scanConnections()
    {
        std::vector<DBConnection *> drained;
        DBConnection *c = nullptr;
        while (idleQueue_.dequeue(c))
        {
            idleCount_.fetch_sub(1, std::memory_order_relaxed);
            drained.push_back(c);
        }

        // 第一轮: 保活检测(连接被维护线程独占, 无锁)
        std::vector<DBConnection *> healthy;
        healthy.reserve(drained.size());
        for (auto *conn : drained)
        {
            if (conn->isKeepaliveTimeout(keepaliveSec_))
            {
                conn->setState(ConnState::Keepalive);
                if (!conn->ping())
                {
                    DB_LOG_WARN("DBPool::scanConnections() keepalive ping failed, discard connection:{}", (void *)conn);
                    discard(conn);
                    continue;
                }
                conn->refreshLastUsedTime();
            }
            healthy.push_back(conn);
        }

        // 第二轮: 超时剔除 + 回队
        size_t keepCount = healthy.size();
        size_t surplus = keepCount > (size_t)minIdle_ ? keepCount - (size_t)minIdle_ : 0;
        bool reenqueued = false;
        for (auto *conn : healthy)
        {
            if (conn->isLifeTimeout(maxLifetimeSec_))
            {
                discard(conn);
                --keepCount;
                continue;
            }
            // 空闲超时且空闲数超出 minIdle 时收缩
            if (conn->isIdleTimeout(idleTimeoutSec_) && surplus > 0)
            {
                --surplus;
                --keepCount;
                discard(conn);
                continue;
            }
            conn->setState(ConnState::Idle);
            idleQueue_.enqueue(conn);
            idleCount_.fetch_add(1, std::memory_order_relaxed);
            reenqueued = true;
        }

        // 有连接回队才唤醒等待者(避免虚假唤醒)
        if (reenqueued && waiterCount_.load(std::memory_order_acquire) > 0)
        {
            std::lock_guard<std::mutex> lock(waitMutex_);
            cvIdleAvailable_.notify_all();
        }

        if (idleCount_.load(std::memory_order_relaxed) < minIdle_)
            needReplenish_.store(true, std::memory_order_relaxed);
    }

    /// 泄漏检测: 快照遍历注册表(≤maxSize 个), 检查借出超时的连接
    void DBPool::scanBorrowedLeaks()
    {
        std::vector<DBConnection *> snapshot;
        {
            std::lock_guard<std::mutex> lock(allConnsMutex_);
            snapshot = allConns_;
        }
        for (auto *conn : snapshot)
        {
            if (conn->getState() == ConnState::InUse &&
                conn->isBorrowedTimeout(borrowedTimeoutSec_))
            {
                auto now = std::chrono::steady_clock::now();
                DB_LOG_WARN("DBConnection:{} borrowed timeout, usage duration:{}s", (void *)conn,
                            std::chrono::duration_cast<std::chrono::seconds>(now - conn->getBorrowedTime()).count());
            }
        }
    }

    void DBPool::replenishToMinIdle()
    {
        int idle = idleCount_.load(std::memory_order_relaxed);
        if (idle >= minIdle_)
            return;
        int need = minIdle_ - idle;
        int available = maxSize_ - totalConnCount_.load(std::memory_order_relaxed);
        if (need > available)
            need = available;
        if (need <= 0)
            return;
        totalConnCount_.fetch_add(need, std::memory_order_relaxed); // 一次性预占名额
        for (int i = 0; i < need; ++i)
            createPool_->submit([this]()
                                { createOne(); });
    }

    void DBPool::createOne()
    {
        IDB::ptr conn = factory_->create(dbconfig_);
        if (!conn || !conn->connect())
        {
            DB_LOG_ERROR("DBPool::createOne() create connection failed");
            totalConnCount_.fetch_sub(1, std::memory_order_relaxed); // 回退预占
            return;
        }
        DBConnection *wrap = new DBConnection(conn);
        {
            std::lock_guard<std::mutex> lock(allConnsMutex_);
            allConns_.push_back(wrap);
        }
        wrap->setState(ConnState::Idle);
        idleQueue_.enqueue(wrap);
        idleCount_.fetch_add(1, std::memory_order_relaxed);
        // 新连接入队后唤醒等待者
        if (waiterCount_.load(std::memory_order_acquire) > 0)
        {
            std::lock_guard<std::mutex> lock(waitMutex_);
            cvIdleAvailable_.notify_all();
        }
    }

    /// 销毁连接: 从注册表移除并释放(创建/借出计数由调用方维护)
    void DBPool::discard(DBConnection *conn)
    {
        totalConnCount_.fetch_sub(1, std::memory_order_relaxed);
        conn->setState(ConnState::Broken);
        {
            std::lock_guard<std::mutex> lock(allConnsMutex_);
            auto it = std::find(allConns_.begin(), allConns_.end(), conn);
            if (it != allConns_.end())
                allConns_.erase(it);
        }
        delete conn;
    }

    void DBPool::notifyMaintenance()
    {
        std::lock_guard<std::mutex> lock(maintenanceMutex_);
        cvMaintenance_.notify_one();
    }

    DBPool::Stats DBPool::stats() const
    {
        Stats s;
        s.total = totalConnCount_.load(std::memory_order_relaxed);
        s.idle = idleCount_.load(std::memory_order_relaxed);
        s.inUse = s.total > s.idle ? s.total - s.idle : 0;
        s.maxSize = maxSize_;
        s.minIdle = minIdle_;
        return s;
    }

} // namespace db

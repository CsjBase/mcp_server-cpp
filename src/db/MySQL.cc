#include "MySQL.h"
#include "Log.h"
#include "config/Config.h"
#include "utils/thread_pool.h"

#include <mysql/errmsg.h>
#include <stdarg.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace db
{

    // static config::ConfigVar<std::map<std::string, std::map<std::string, std::string>>>::ptr g_mysql_dbs =
    // config::Config::lookup("mysql.dbs",
    //                         std::map<std::string, std::map<std::string, std::string>>(),
    //                         "mysql dbs");

    bool mysql_time_to_time_t(const MYSQL_TIME &mt, time_t &ts)
    {
        struct tm tm;
        ts = 0;
        localtime_r(&ts, &tm);
        tm.tm_year = mt.year - 1900;
        tm.tm_mon = mt.month - 1;
        tm.tm_mday = mt.day;
        tm.tm_hour = mt.hour;
        tm.tm_min = mt.minute;
        tm.tm_sec = mt.second;
        ts = mktime(&tm);
        if (ts < 0)
        {
            ts = 0;
        }
        return true;
    }

    bool time_t_to_mysql_time(const time_t &ts, MYSQL_TIME &mt)
    {
        struct tm tm;
        localtime_r(&ts, &tm);
        mt.year = tm.tm_year + 1900;
        mt.month = tm.tm_mon + 1;
        mt.day = tm.tm_mday;
        mt.hour = tm.tm_hour;
        mt.minute = tm.tm_min;
        mt.second = tm.tm_sec;
        return true;
    }

    namespace
    {
        struct MySQLThreadIniter
        {
            MySQLThreadIniter() { mysql_thread_init(); }
            ~MySQLThreadIniter() { mysql_thread_end(); }
        };

        // 统一 ColumnType 映射
        ColumnType toColumnType(enum_field_types t)
        {
            switch (t)
            {
            case MYSQL_TYPE_TINY:
                return ColumnType::TINY;
            case MYSQL_TYPE_SHORT:
                return ColumnType::SHORT;
            case MYSQL_TYPE_LONG:
                return ColumnType::LONG;
            case MYSQL_TYPE_LONGLONG:
                return ColumnType::LONGLONG;
            case MYSQL_TYPE_FLOAT:
                return ColumnType::FLOAT;
            case MYSQL_TYPE_DOUBLE:
                return ColumnType::DOUBLE;
            case MYSQL_TYPE_STRING:
            case MYSQL_TYPE_VAR_STRING:
            case MYSQL_TYPE_VARCHAR:
                return ColumnType::STRING;
            case MYSQL_TYPE_BLOB:
            case MYSQL_TYPE_TINY_BLOB:
            case MYSQL_TYPE_MEDIUM_BLOB:
            case MYSQL_TYPE_LONG_BLOB:
                return ColumnType::BLOB;
            case MYSQL_TYPE_TIMESTAMP:
                return ColumnType::TIMESTAMP;
            case MYSQL_TYPE_DATETIME:
                return ColumnType::DATETIME;
            case MYSQL_TYPE_DATE:
                return ColumnType::DATE;
            case MYSQL_TYPE_TIME:
                return ColumnType::TIME;
            case MYSQL_TYPE_NULL:
                return ColumnType::NULL_TYPE;
            default:
                return ColumnType::UNKNOWN;
            }
        }

        // 安全的 vasprintf 包装，失败返回空串
        std::string safeVFormat(const char *format, va_list ap)
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
    }

    static MYSQL *mysql_init(const DBConfig &config, const int &timeout)
    {
        static thread_local MySQLThreadIniter s_thread_initer;

        MYSQL *mysql = ::mysql_init(nullptr);
        if (mysql == nullptr)
        {
            DB_LOG_ERROR("mysql_init error");
            return nullptr;
        }

        if (timeout > 0)
        {
            mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        }
        // bool reconnect = false;
        // mysql_options(mysql, MYSQL_OPT_RECONNECT, &reconnect);
        mysql_options(mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");

        int port = config.port;
        std::string host = config.host;
        std::string user = config.user;
        std::string passwd = config.password;
        std::string dbname = config.dbname;

        if (mysql_real_connect(mysql, host.c_str(), user.c_str(), passwd.c_str(),
                               dbname.c_str(), port, NULL, 0) == nullptr)
        {
            DB_LOG_ERROR("mysql_real_connect({}, {}, {}) error: {}", host, port, dbname, mysql_error(mysql));
            mysql_close(mysql);
            return nullptr;
        }
        return mysql;
    }

    MySQL::MySQL(const DBConfig &config)
        : m_config(config), m_lastUsedTime(0), m_hasError(false)
    {
        DB_LOG_DEBUG("Create MySQL*: {}", (void *)this);
    }

    MySQL::~MySQL()
    {
        DB_LOG_DEBUG("Destroy MySQL*: {}", (void *)this);
    }

    bool MySQL::connect()
    {
        if (m_mysql && !m_hasError)
        {
            return true;
        }

        MYSQL *m = mysql_init(m_config, 0);
        if (!m)
        {
            m_hasError = true;
            return false;
        }
        m_hasError = false;
        m_mysql.reset(m, mysql_close);
        return true;
    }

    bool MySQL::isValid()
    {
        if (!m_mysql)
            return false;
        if (m_hasError && getErrno() == CR_SERVER_GONE_ERROR)
        {
            return false;
        }
        // int result = mysql_ping(m_mysql.get());
        // if (result != 0)
        // {
        //     m_hasError = true;
        //     return false;
        // }
        // m_hasError = false;
        return true;
    }

    bool MySQL::ping()
    {
        if (!isValid())
        {
            return false;
        }
        if (mysql_ping(m_mysql.get()))
        {
            m_hasError = true;
            return false;
        }
        m_hasError = false;
        return true;
    }

    IStmt::ptr MySQL::prepare(const std::string &sql)
    {
        return MySQLStmt::Create(shared_from_this(), sql);
    }

    ITransaction::ptr MySQL::openTransaction(bool auto_commit)
    {
        return MySQLTransaction::Create(shared_from_this(), auto_commit);
    }

    int64_t MySQL::getLastInsertId()
    {
        return mysql_insert_id(m_mysql.get());
    }

    bool MySQL::isNeedCheck()
    {
        if ((time(0) - m_lastUsedTime) < 5 && !m_hasError)
        {
            return false;
        }
        return true;
    }

    int MySQL::execute(const char *format, ...)
    {
        va_list ap;
        va_start(ap, format);
        int rt = execute(format, ap);
        va_end(ap);
        return rt;
    }

    int MySQL::execute(const char *format, va_list ap)
    {
        std::string sql = safeVFormat(format, ap);
        if (sql.empty() && format && *format)
        {
            // 格式化失败
            m_cmd.clear();
            m_hasError = true;
            return -1;
        }
        m_cmd = sql;
        int r = ::mysql_query(m_mysql.get(), m_cmd.c_str());
        if (r)
        {
            DB_LOG_ERROR("cmd={}, error:{}", cmd(), getErrStr());
            m_hasError = true;
        }
        else
        {
            m_hasError = false;
        }
        return r;
    }

    int MySQL::execute(const std::string &sql)
    {
        m_cmd = sql;
        int r = ::mysql_query(m_mysql.get(), m_cmd.c_str());
        if (r)
        {
            DB_LOG_ERROR("cmd={}, error:{}", cmd(), getErrStr());
            m_hasError = true;
        }
        else
        {
            m_hasError = false;
        }
        return r;
    }

    std::shared_ptr<MySQL> MySQL::getMySQL()
    {
        // 关键修复：使用 shared_from_this，避免悬垂
        return shared_from_this();
    }

    std::shared_ptr<MYSQL> MySQL::getRaw()
    {
        return m_mysql;
    }

    uint64_t MySQL::getAffectedRows()
    {
        if (!m_mysql)
            return 0;
        return mysql_affected_rows(m_mysql.get());
    }

    // 上面占位函数用于避免笔误，下面给出正确实现
    // （保留函数名，删除占位后使用下面版本）
    static MYSQL_RES *my_mysql_query(MYSQL *mysql, const char *sql)
    {
        if (mysql == nullptr)
        {
            DB_LOG_ERROR("mysql_query mysql is null");
            return nullptr;
        }
        if (sql == nullptr)
        {
            DB_LOG_ERROR("mysql_query sql is null");
            return nullptr;
        }
        if (::mysql_query(mysql, sql))
        {
            DB_LOG_ERROR("mysql_query({}) error:{}", sql, mysql_error(mysql));
            return nullptr;
        }
        MYSQL_RES *res = mysql_store_result(mysql);
        if (res == nullptr)
        {
            DB_LOG_ERROR("mysql_store_result() error:{}", mysql_error(mysql));
        }
        return res;
    }

    MySQLStmt::ptr MySQLStmt::Create(MySQL::ptr db, const std::string &stmt)
    {
        auto st = mysql_stmt_init(db->getRaw().get());
        if (!st)
        {
            return nullptr;
        }
        if (mysql_stmt_prepare(st, stmt.c_str(), stmt.size()))
        {
            DB_LOG_ERROR("stmt={} errno={} errstr={}", stmt, mysql_stmt_errno(st), mysql_stmt_error(st));
            mysql_stmt_close(st);
            return nullptr;
        }
        int count = mysql_stmt_param_count(st);
        MySQLStmt::ptr rt(new MySQLStmt(db, st));
        rt->m_binds.resize(count);
        rt->m_buffers.resize(count);
        memset(&rt->m_binds[0], 0, sizeof(rt->m_binds[0]) * count);
        return rt;
    }

    MySQLStmt::MySQLStmt(MySQL::ptr db, MYSQL_STMT *stmt)
        : m_mysql(db), m_stmt(stmt)
    {
    }

    MySQLStmt::~MySQLStmt()
    {
        if (m_stmt)
        {
            mysql_stmt_close(m_stmt);
        }
        // m_buffers 由 vector<char> 自动管理，不再手动 free
    }

    int MySQLStmt::bind(int idx, const int8_t &value) { return bindInt8(idx, value); }
    int MySQLStmt::bind(int idx, const uint8_t &value) { return bindUint8(idx, value); }
    int MySQLStmt::bind(int idx, const int16_t &value) { return bindInt16(idx, value); }
    int MySQLStmt::bind(int idx, const uint16_t &value) { return bindUint16(idx, value); }
    int MySQLStmt::bind(int idx, const int32_t &value) { return bindInt32(idx, value); }
    int MySQLStmt::bind(int idx, const uint32_t &value) { return bindUint32(idx, value); }
    int MySQLStmt::bind(int idx, const int64_t &value) { return bindInt64(idx, value); }
    int MySQLStmt::bind(int idx, const uint64_t &value) { return bindUint64(idx, value); }
    int MySQLStmt::bind(int idx, const float &value) { return bindFloat(idx, value); }
    int MySQLStmt::bind(int idx, const double &value) { return bindDouble(idx, value); }
    int MySQLStmt::bind(int idx, const std::string &value) { return bindString(idx, value); }
    int MySQLStmt::bind(int idx, const char *value) { return bindString(idx, value); }
    int MySQLStmt::bind(int idx, const void *value, int len) { return bindBlob(idx, value, len); }

    int MySQLStmt::bind(int idx)
    {
        if (!checkIdx(idx))
            return -1;
        idx -= 1;
        m_binds[idx].buffer_type = MYSQL_TYPE_NULL;
        m_binds[idx].buffer = nullptr;
        m_binds[idx].buffer_length = 0;
        m_binds[idx].length = nullptr;
        return 0;
    }

    int MySQLStmt::getErrno()
    {
        return mysql_stmt_errno(m_stmt);
    }

    std::string MySQLStmt::getErrStr()
    {
        const char *e = mysql_stmt_error(m_stmt);
        return e ? e : "";
    }

    int MySQLStmt::bindNull(int idx) { return bind(idx); }

    // 统一：把数据拷到 m_buffers[idx] 并绑定
#define BIND_IMPL(TYPE, MYSQL_TYPE_ENUM, UNSIGNED)       \
    if (!checkIdx(idx))                                  \
        return -1;                                       \
    idx -= 1;                                            \
    m_buffers[idx].resize(sizeof(TYPE));                 \
    memcpy(m_buffers[idx].data(), &value, sizeof(TYPE)); \
    m_binds[idx].buffer_type = MYSQL_TYPE_ENUM;          \
    m_binds[idx].buffer = m_buffers[idx].data();         \
    m_binds[idx].buffer_length = sizeof(TYPE);           \
    m_binds[idx].is_unsigned = UNSIGNED;                 \
    m_binds[idx].length = nullptr;                       \
    return 0;

    int MySQLStmt::bindInt8(int idx, const int8_t &value) { BIND_IMPL(int8_t, MYSQL_TYPE_TINY, 0) }
    int MySQLStmt::bindUint8(int idx, const uint8_t &value) { BIND_IMPL(uint8_t, MYSQL_TYPE_TINY, 1) }
    int MySQLStmt::bindInt16(int idx, const int16_t &value) { BIND_IMPL(int16_t, MYSQL_TYPE_SHORT, 0) }
    int MySQLStmt::bindUint16(int idx, const uint16_t &value) { BIND_IMPL(uint16_t, MYSQL_TYPE_SHORT, 1) }
    int MySQLStmt::bindInt32(int idx, const int32_t &value) { BIND_IMPL(int32_t, MYSQL_TYPE_LONG, 0) }
    int MySQLStmt::bindUint32(int idx, const uint32_t &value) { BIND_IMPL(uint32_t, MYSQL_TYPE_LONG, 1) }
    int MySQLStmt::bindInt64(int idx, const int64_t &value) { BIND_IMPL(int64_t, MYSQL_TYPE_LONGLONG, 0) }
    int MySQLStmt::bindUint64(int idx, const uint64_t &value) { BIND_IMPL(uint64_t, MYSQL_TYPE_LONGLONG, 1) }
    int MySQLStmt::bindFloat(int idx, const float &value) { BIND_IMPL(float, MYSQL_TYPE_FLOAT, 0) }
    int MySQLStmt::bindDouble(int idx, const double &value) { BIND_IMPL(double, MYSQL_TYPE_DOUBLE, 0) }

#undef BIND_IMPL

    // 字符串/二进制统一处理：每次绑定显式更新 buffer_length 与 length
    static int bindStringImpl(std::vector<MYSQL_BIND> &binds,
                              std::vector<std::vector<char>> &buffers,
                              int idx, const char *data, size_t size,
                              enum_field_types type)
    {
        // 索引检查由调用方完成
        idx -= 1;
        buffers[idx].resize(size);
        if (size > 0)
        {
            memcpy(buffers[idx].data(), data, size);
        }
        binds[idx].buffer_type = type;
        binds[idx].buffer = buffers[idx].data();
        binds[idx].buffer_length = (unsigned long)size;
        binds[idx].length = &binds[idx].buffer_length; // 关键：指向当前长度
        binds[idx].is_unsigned = 0;
        return 0;
    }

    int MySQLStmt::bindString(int idx, const char *value)
    {
        if (!checkIdx(idx) || value == nullptr)
            return -1;
        return bindStringImpl(m_binds, m_buffers, idx, value, strlen(value), MYSQL_TYPE_STRING);
    }

    int MySQLStmt::bindString(int idx, const std::string &value)
    {
        if (!checkIdx(idx))
            return -1;
        return bindStringImpl(m_binds, m_buffers, idx, value.c_str(), value.size(), MYSQL_TYPE_STRING);
    }

    int MySQLStmt::bindBlob(int idx, const void *value, int64_t size)
    {
        if (!checkIdx(idx) || (value == nullptr && size > 0) || size < 0)
            return -1;
        return bindStringImpl(m_binds, m_buffers, idx,
                              static_cast<const char *>(value),
                              static_cast<size_t>(size),
                              MYSQL_TYPE_BLOB);
    }

    int MySQLStmt::bindBlob(int idx, const std::string &value)
    {
        if (!checkIdx(idx))
            return -1;
        return bindStringImpl(m_binds, m_buffers, idx, value.c_str(), value.size(), MYSQL_TYPE_BLOB);
    }

    int MySQLStmt::bindTime(int idx, const time_t &value)
    {
        if (!checkIdx(idx))
            return -1;
        // 用 MYSQL_TYPE_TIMESTAMP + MYSQL_TIME，避免字符串格式不一致
        idx -= 1;
        m_buffers[idx].resize(sizeof(MYSQL_TIME));
        MYSQL_TIME *mt = reinterpret_cast<MYSQL_TIME *>(m_buffers[idx].data());
        memset(mt, 0, sizeof(MYSQL_TIME));
        time_t_to_mysql_time(value, *mt);
        m_binds[idx].buffer_type = MYSQL_TYPE_TIMESTAMP;
        m_binds[idx].buffer = mt;
        m_binds[idx].buffer_length = sizeof(MYSQL_TIME);
        m_binds[idx].length = nullptr;
        m_binds[idx].is_unsigned = 0;
        return 0;
    }

    int MySQLStmt::execute()
    {
        if (m_binds.empty())
        {
            // 无参数时也允许执行
            return mysql_stmt_execute(m_stmt);
        }
        if (mysql_stmt_bind_param(m_stmt, &m_binds[0]))
        {
            return -1;
        }
        return mysql_stmt_execute(m_stmt);
    }

    int64_t MySQLStmt::getLastInsertId()
    {
        return mysql_stmt_insert_id(m_stmt);
    }

    ISQLData::ptr MySQLStmt::query()
    {
        if (!m_binds.empty() && mysql_stmt_bind_param(m_stmt, &m_binds[0]))
        {
            return nullptr;
        }
        return MySQLStmtRes::Create(shared_from_this());
    }

    // ---------------- MySQLRes ----------------

    MySQLRes::MySQLRes(MYSQL_RES *res, int eno, const char *estr)
        : m_errno(eno), m_errstr(estr ? estr : ""),
          m_cur(nullptr), m_curLength(nullptr), m_fieldCount(0)
    {
        if (res)
        {
            m_data.reset(res, mysql_free_result);
            m_fieldCount = mysql_num_fields(res);
            MYSQL_FIELD *fields = mysql_fetch_fields(res);
            if (fields && m_fieldCount > 0)
            {
                m_fields.reset(fields, [](MYSQL_FIELD *) {});
            }
        }
    }

    bool MySQLRes::foreach (data_cb cb)
    {
        MYSQL_ROW row;
        uint64_t fields = getColumnCount();
        int i = 0;
        while ((row = mysql_fetch_row(m_data.get())))
        {
            if (!cb(row, fields, i++))
            {
                break;
            }
        }
        return true;
    }

    int MySQLRes::getDataCount()
    {
        return m_data ? mysql_num_rows(m_data.get()) : 0;
    }

    int MySQLRes::getColumnCount()
    {
        return m_fieldCount;
    }

    int MySQLRes::getColumnBytes(int idx)
    {
        if (!m_curLength || idx < 0 || idx >= m_fieldCount)
            return 0;
        return (int)m_curLength[idx];
    }

    ColumnType MySQLRes::getColumnType(int idx)
    {
        if (!m_fields || idx < 0 || idx >= m_fieldCount)
            return ColumnType::UNKNOWN;
        return toColumnType(m_fields.get()[idx].type);
    }

    std::string MySQLRes::getColumnName(int idx)
    {
        if (!m_fields || idx < 0 || idx >= m_fieldCount)
            return "";
        return m_fields.get()[idx].name ? m_fields.get()[idx].name : "";
    }

    bool MySQLRes::isNull(int idx)
    {
        if (!m_cur || idx < 0 || idx >= m_fieldCount)
            return true;
        return m_cur[idx] == nullptr;
    }

    int8_t MySQLRes::getInt8(int idx) { return (int8_t)getInt64(idx); }
    uint8_t MySQLRes::getUint8(int idx) { return (uint8_t)getInt64(idx); }
    int16_t MySQLRes::getInt16(int idx) { return (int16_t)getInt64(idx); }
    uint16_t MySQLRes::getUint16(int idx) { return (uint16_t)getInt64(idx); }
    int32_t MySQLRes::getInt32(int idx) { return (int32_t)getInt64(idx); }
    uint32_t MySQLRes::getUint32(int idx) { return (uint32_t)getInt64(idx); }

    int64_t MySQLRes::getInt64(int idx)
    {
        if (isNull(idx))
            return 0;
        return strtoll(m_cur[idx], nullptr, 10);
    }

    uint64_t MySQLRes::getUint64(int idx)
    {
        if (isNull(idx))
            return 0;
        return strtoull(m_cur[idx], nullptr, 10);
    }

    float MySQLRes::getFloat(int idx) { return (float)getDouble(idx); }
    double MySQLRes::getDouble(int idx)
    {
        if (isNull(idx))
            return 0;
        return atof(m_cur[idx]);
    }

    std::string MySQLRes::getString(int idx)
    {
        if (isNull(idx))
            return "";
        return std::string(m_cur[idx], m_curLength[idx]);
    }

    std::string MySQLRes::getBlob(int idx)
    {
        if (isNull(idx))
            return "";
        return std::string(m_cur[idx], m_curLength[idx]);
    }

    time_t MySQLRes::getTime(int idx)
    {
        if (isNull(idx))
            return 0;
        struct tm t;
        memset(&t, 0, sizeof(t));
        if (!strptime(m_cur[idx], "%Y-%m-%d %H:%M:%S", &t))
        {
            return 0;
        }
        return mktime(&t);
    }

    bool MySQLRes::next()
    {
        if (!m_data)
            return false;
        m_cur = mysql_fetch_row(m_data.get());
        m_curLength = mysql_fetch_lengths(m_data.get());
        return m_cur != nullptr;
    }

    // ---------------- MySQLStmtRes ----------------

    MySQLStmtRes::ptr MySQLStmtRes::Create(std::shared_ptr<MySQLStmt> stmt)
    {
        int eno = mysql_stmt_errno(stmt->getRaw());
        const char *errstr = mysql_stmt_error(stmt->getRaw());
        MySQLStmtRes::ptr rt(new MySQLStmtRes(stmt, eno, errstr ? errstr : ""));
        if (eno)
        {
            return rt;
        }
        MYSQL_RES *res = mysql_stmt_result_metadata(stmt->getRaw());
        if (!res)
        {
            return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
        }
        // 用 shared_ptr 托管元数据
        std::shared_ptr<MYSQL_RES> meta(res, mysql_free_result);

        int num = mysql_num_fields(res);
        MYSQL_FIELD *fields = mysql_fetch_fields(res);

        rt->m_binds.resize(num);
        memset(&rt->m_binds[0], 0, sizeof(rt->m_binds[0]) * num);
        rt->m_datas.resize(num);
        rt->m_fieldNames.resize(num);
        rt->m_colTypes.resize(num);

        for (int i = 0; i < num; ++i)
        {
            rt->m_datas[i].type = fields[i].type;
            rt->m_colTypes[i] = toColumnType(fields[i].type);
            rt->m_fieldNames[i] = fields[i].name ? fields[i].name : "";
            switch (fields[i].type)
            {
#define XX(m, t)                         \
    case m:                              \
        rt->m_datas[i].alloc(sizeof(t)); \
        break;
                XX(MYSQL_TYPE_TINY, int8_t);
                XX(MYSQL_TYPE_SHORT, int16_t);
                XX(MYSQL_TYPE_LONG, int32_t);
                XX(MYSQL_TYPE_LONGLONG, int64_t);
                XX(MYSQL_TYPE_FLOAT, float);
                XX(MYSQL_TYPE_DOUBLE, double);
                XX(MYSQL_TYPE_TIMESTAMP, MYSQL_TIME);
                XX(MYSQL_TYPE_DATETIME, MYSQL_TIME);
                XX(MYSQL_TYPE_DATE, MYSQL_TIME);
                XX(MYSQL_TYPE_TIME, MYSQL_TIME);
#undef XX
            default:
                rt->m_datas[i].alloc(fields[i].length > 0 ? fields[i].length : 1);
                break;
            }

            rt->m_binds[i].buffer_type = rt->m_datas[i].type;
            rt->m_binds[i].buffer = rt->m_datas[i].data;
            rt->m_binds[i].buffer_length = rt->m_datas[i].data_length;
            rt->m_binds[i].length = &rt->m_datas[i].length;
            rt->m_binds[i].is_null = &rt->m_datas[i].is_null;
            rt->m_binds[i].error = &rt->m_datas[i].error;
        }

        if (mysql_stmt_bind_result(stmt->getRaw(), &rt->m_binds[0]))
        {
            return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
        }

        if (stmt->execute())
        {
            return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
        }

        if (mysql_stmt_store_result(stmt->getRaw()))
        {
            return MySQLStmtRes::ptr(new MySQLStmtRes(stmt, stmt->getErrno(), stmt->getErrStr()));
        }
        return rt;
    }

    int MySQLStmtRes::getDataCount()
    {
        return mysql_stmt_num_rows(m_stmt->getRaw());
    }

    int MySQLStmtRes::getColumnCount()
    {
        return (int)m_datas.size();
    }

    int MySQLStmtRes::getColumnBytes(int idx)
    {
        if (idx < 0 || idx >= (int)m_datas.size())
            return 0;
        return (int)m_datas[idx].length;
    }

    ColumnType MySQLStmtRes::getColumnType(int idx)
    {
        if (idx < 0 || idx >= (int)m_colTypes.size())
            return ColumnType::UNKNOWN;
        return m_colTypes[idx];
    }

    std::string MySQLStmtRes::getColumnName(int idx)
    {
        if (idx < 0 || idx >= (int)m_fieldNames.size())
            return "";
        return m_fieldNames[idx];
    }

    bool MySQLStmtRes::isNull(int idx)
    {
        if (idx < 0 || idx >= (int)m_datas.size())
            return true;
        return m_datas[idx].is_null;
    }

#define XX(type)        \
    if (isNull(idx))    \
        return type(0); \
    return *(type *)m_datas[idx].data
    int8_t MySQLStmtRes::getInt8(int idx) { XX(int8_t); }
    uint8_t MySQLStmtRes::getUint8(int idx) { XX(uint8_t); }
    int16_t MySQLStmtRes::getInt16(int idx) { XX(int16_t); }
    uint16_t MySQLStmtRes::getUint16(int idx) { XX(uint16_t); }
    int32_t MySQLStmtRes::getInt32(int idx) { XX(int32_t); }
    uint32_t MySQLStmtRes::getUint32(int idx) { XX(uint32_t); }
    int64_t MySQLStmtRes::getInt64(int idx) { XX(int64_t); }
    uint64_t MySQLStmtRes::getUint64(int idx) { XX(uint64_t); }
    float MySQLStmtRes::getFloat(int idx) { XX(float); }
    double MySQLStmtRes::getDouble(int idx) { XX(double); }
#undef XX

    std::string MySQLStmtRes::getString(int idx)
    {
        if (isNull(idx))
            return "";
        return std::string(m_datas[idx].data, m_datas[idx].length);
    }

    std::string MySQLStmtRes::getBlob(int idx)
    {
        if (isNull(idx))
            return "";
        return std::string(m_datas[idx].data, m_datas[idx].length);
    }

    time_t MySQLStmtRes::getTime(int idx)
    {
        if (isNull(idx))
            return 0;
        MYSQL_TIME *v = (MYSQL_TIME *)m_datas[idx].data;
        time_t ts = 0;
        mysql_time_to_time_t(*v, ts);
        return ts;
    }

    bool MySQLStmtRes::next()
    {
        return !mysql_stmt_fetch(m_stmt->getRaw());
    }

    MySQLStmtRes::Data::Data()
        : is_null(0), error(0), type(), length(0), data_length(0), data(nullptr)
    {
    }

    MySQLStmtRes::Data::~Data()
    {
        delete[] data;
        data = nullptr;
    }

    void MySQLStmtRes::Data::alloc(size_t size)
    {
        delete[] data;
        data = new char[size]();
        length = size;
        data_length = (int32_t)size;
    }

    MySQLStmtRes::MySQLStmtRes(std::shared_ptr<MySQLStmt> stmt, int eno, const std::string &estr)
        : m_errno(eno), m_errstr(estr), m_stmt(stmt)
    {
    }

    MySQLStmtRes::~MySQLStmtRes()
    {
        if (!m_errno && m_stmt)
        {
            mysql_stmt_free_result(m_stmt->getRaw());
        }
    }

    ISQLData::ptr MySQL::query(const char *format, ...)
    {
        va_list ap;
        va_start(ap, format);
        auto rt = query(format, ap);
        va_end(ap);
        return rt;
    }

    ISQLData::ptr MySQL::query(const char *format, va_list ap)
    {
        std::string sql = safeVFormat(format, ap);
        if (sql.empty() && format && *format)
        {
            m_cmd.clear();
            m_hasError = true;
            return nullptr;
        }
        return query(sql);
    }

    ISQLData::ptr MySQL::query(const std::string &sql)
    {
        m_cmd = sql;
        MYSQL_RES *res = my_mysql_query(m_mysql.get(), m_cmd.c_str());
        if (!res)
        {
            m_hasError = true;
            return nullptr;
        }
        m_hasError = false;
        ISQLData::ptr rt(new MySQLRes(res, mysql_errno(m_mysql.get()), mysql_error(m_mysql.get())));
        return rt;
    }

    const char *MySQL::cmd()
    {
        return m_cmd.c_str();
    }

    bool MySQL::use(const std::string &dbname)
    {
        if (!m_mysql)
            return false;
        if (m_dbname == dbname)
            return true;
        if (mysql_select_db(m_mysql.get(), dbname.c_str()) == 0)
        {
            m_dbname = dbname;
            m_hasError = false;
            return true;
        }
        m_dbname.clear();
        m_hasError = true;
        return false;
    }

    std::string MySQL::getErrStr()
    {
        if (!m_mysql)
            return "mysql is null";
        const char *str = mysql_error(m_mysql.get());
        return str ? str : "";
    }

    int MySQL::getErrno()
    {
        if (!m_mysql)
            return -1;
        return mysql_errno(m_mysql.get());
    }

    uint64_t MySQL::getInsertId()
    {
        if (m_mysql)
            return mysql_insert_id(m_mysql.get());
        return 0;
    }

    // ---------------- MySQLTransaction ----------------

    MySQLTransaction::ptr MySQLTransaction::Create(MySQL::ptr mysql, bool auto_commit)
    {
        MySQLTransaction::ptr rt(new MySQLTransaction(mysql, auto_commit));
        if (rt->begin())
        {
            return rt;
        }
        return nullptr;
    }

    MySQLTransaction::~MySQLTransaction()
    {
        if (m_autoCommit)
            commit();
        else
            rollback();
    }

    int64_t MySQLTransaction::getLastInsertId()
    {
        return m_mysql->getLastInsertId();
    }

    uint64_t MySQLTransaction::getAffectedRows()
    {
        return m_mysql->getAffectedRows();
    }

    ISQLData::ptr MySQLTransaction::query(const char *format, ...)
    {
        va_list ap;
        va_start(ap, format);
        auto rt = query(format, ap);
        va_end(ap);
        return rt;
    }

    ISQLData::ptr MySQLTransaction::query(const char *format, va_list ap)
    {
        if (m_isFinished)
        {
            DB_LOG_ERROR("transaction is finished");
            return nullptr;
        }
        auto rt = m_mysql->query(format, ap);
        if (rt == nullptr)
            m_hasError = true;
        return rt;
    }

    ISQLData::ptr MySQLTransaction::query(const std::string &sql)
    {
        if (m_isFinished)
        {
            DB_LOG_ERROR("transaction is finished, sql=", sql);
            return nullptr;
        }
        auto rt = m_mysql->query(sql);
        if (rt == nullptr)
            m_hasError = true;
        return rt;
    }

    bool MySQLTransaction::begin()
    {
        return execute("BEGIN") == 0;
    }

    bool MySQLTransaction::commit()
    {
        if (m_isFinished || m_hasError)
            return !m_hasError;
        int rt = execute("COMMIT");
        if (rt == 0)
            m_isFinished = true;
        else
            m_hasError = true;
        return rt == 0;
    }

    bool MySQLTransaction::rollback()
    {
        if (m_isFinished)
            return true;
        int rt = execute("ROLLBACK");
        if (rt == 0)
            m_isFinished = true;
        else
            m_hasError = true;
        return rt == 0;
    }

    IStmt::ptr MySQLTransaction::prepare(const std::string &stmt)
    {
        return m_mysql->prepare(stmt);
    }

    int MySQLTransaction::execute(const char *format, ...)
    {
        va_list ap;
        va_start(ap, format);
        auto rt = execute(format, ap);
        va_end(ap);
        return rt;
    }

    int MySQLTransaction::execute(const char *format, va_list ap)
    {
        if (m_isFinished)
        {
            DB_LOG_ERROR("transaction is finished");
            return -1;
        }
        int rt = m_mysql->execute(format, ap);
        if (rt)
            m_hasError = true;
        return rt;
    }

    int MySQLTransaction::execute(const std::string &sql)
    {
        if (m_isFinished)
        {
            DB_LOG_ERROR("transaction is finished, sql={}", sql);
            return -1;
        }
        int rt = m_mysql->execute(sql);
        if (rt)
            m_hasError = true;
        return rt;
    }

    std::shared_ptr<MySQL> MySQLTransaction::getMySQL() { return m_mysql; }
    int MySQLTransaction::getErrno() { return m_mysql->getErrno(); }
    std::string MySQLTransaction::getErrStr() { return m_mysql->getErrStr(); }

    MySQLTransaction::MySQLTransaction(MySQL::ptr mysql, bool auto_commit)
        : m_mysql(mysql), m_autoCommit(auto_commit), m_isFinished(false), m_hasError(false)
    {
    }

}
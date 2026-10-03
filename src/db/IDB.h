#pragma once

#include <memory>
#include <string>
#include <ctime>
#include <cstdint>

namespace db
{

    // 统一列类型枚举，避免各实现返回语义不一致的 int
    enum class ColumnType
    {
        UNKNOWN = 0,
        TINY,
        SHORT,
        LONG,
        LONGLONG,
        FLOAT,
        DOUBLE,
        STRING,
        BLOB,
        TIMESTAMP,
        DATETIME,
        DATE,
        TIME,
        NULL_TYPE
    };

    // SQL查询结果集
    class ISQLData
    {
    public:
        typedef std::shared_ptr<ISQLData> ptr;
        virtual ~ISQLData() {}

        virtual int getErrno() const = 0;
        virtual const std::string &getErrStr() const = 0;

        virtual int getDataCount() = 0;
        virtual int getColumnCount() = 0;
        virtual int getColumnBytes(int idx) = 0;
        virtual ColumnType getColumnType(int idx) = 0;
        virtual std::string getColumnName(int idx) = 0;

        virtual bool isNull(int idx) = 0;
        virtual int8_t getInt8(int idx) = 0;
        virtual uint8_t getUint8(int idx) = 0;
        virtual int16_t getInt16(int idx) = 0;
        virtual uint16_t getUint16(int idx) = 0;
        virtual int32_t getInt32(int idx) = 0;
        virtual uint32_t getUint32(int idx) = 0;
        virtual int64_t getInt64(int idx) = 0;
        virtual uint64_t getUint64(int idx) = 0;
        virtual float getFloat(int idx) = 0;
        virtual double getDouble(int idx) = 0;
        virtual std::string getString(int idx) = 0;
        virtual std::string getBlob(int idx) = 0;
        virtual time_t getTime(int idx) = 0;
        virtual bool next() = 0;
    };

    // 更新操作接口（INSERT/UPDATE/DELETE）
    // 注意：execute(const char* format, ...) 仅用于受信 SQL，不防注入；
    //       需要防注入请使用 IStmt 绑定参数。
    class ISQLUpdate
    {
    public:
        virtual ~ISQLUpdate() {}
        virtual int execute(const char *format, ...) = 0;
        virtual int execute(const std::string &sql) = 0;
        virtual uint64_t getAffectedRows() = 0;
        virtual int64_t getLastInsertId() = 0;
    };

    // 查询操作接口（SELECT）
    class ISQLQuery
    {
    public:
        virtual ~ISQLQuery() {}
        virtual ISQLData::ptr query(const char *format, ...) = 0;
        virtual ISQLData::ptr query(const std::string &sql) = 0;
    };

    // 预处理语句接口 防止SQL注入，提升重复查询效率
    class IStmt
    {
    public:
        typedef std::shared_ptr<IStmt> ptr;
        virtual ~IStmt() {}
        virtual int bindInt8(int idx, const int8_t &value) = 0;
        virtual int bindUint8(int idx, const uint8_t &value) = 0;
        virtual int bindInt16(int idx, const int16_t &value) = 0;
        virtual int bindUint16(int idx, const uint16_t &value) = 0;
        virtual int bindInt32(int idx, const int32_t &value) = 0;
        virtual int bindUint32(int idx, const uint32_t &value) = 0;
        virtual int bindInt64(int idx, const int64_t &value) = 0;
        virtual int bindUint64(int idx, const uint64_t &value) = 0;
        virtual int bindFloat(int idx, const float &value) = 0;
        virtual int bindDouble(int idx, const double &value) = 0;
        virtual int bindString(int idx, const char *value) = 0;
        virtual int bindString(int idx, const std::string &value) = 0;
        virtual int bindBlob(int idx, const void *value, int64_t size) = 0;
        virtual int bindBlob(int idx, const std::string &value) = 0;
        virtual int bindTime(int idx, const time_t &value) = 0;
        virtual int bindNull(int idx) = 0;

        virtual int execute() = 0;
        virtual int64_t getLastInsertId() = 0;
        virtual ISQLData::ptr query() = 0;

        virtual int getErrno() = 0;
        virtual std::string getErrStr() = 0;
    };

    // 事务接口
    class ITransaction : public ISQLUpdate, public ISQLQuery
    {
    public:
        typedef std::shared_ptr<ITransaction> ptr;
        virtual ~ITransaction() {};
        virtual bool begin() = 0;
        virtual bool commit() = 0;
        virtual bool rollback() = 0;

        virtual IStmt::ptr prepare(const std::string &stmt) = 0;

        virtual int getErrno() = 0;
        virtual std::string getErrStr() = 0;
    };

    // 数据库主接口
    class IDB : public ISQLUpdate, public ISQLQuery
    {
    public:
        typedef std::shared_ptr<IDB> ptr;

        virtual ~IDB() {}

        virtual IStmt::ptr prepare(const std::string &stmt) = 0;
        virtual int getErrno() = 0;
        virtual std::string getErrStr() = 0;
        virtual ITransaction::ptr openTransaction(bool auto_commit = false) = 0;

        virtual bool connect() = 0;
        virtual bool isValid() = 0;
        virtual bool ping() = 0;
        virtual uint64_t getInsertId() = 0;
    };

}
#pragma once

#include <vector>
#include <string>
#include <string.h>
#include <assert.h>
// #include <algorithm>

#include "net/base/endian.h"

namespace net
{

    class Buffer
    {
    public:
        static const size_t KCheapPrepend = 8;
        static const size_t KInitialSize = 1024;

        explicit Buffer(size_t initialSize = KInitialSize)
            : m_buffer(KCheapPrepend + initialSize), m_readerIndex(KCheapPrepend), m_writerIndex(KCheapPrepend)
        {
        }

        explicit Buffer(const char *readerBuf, size_t len)
            : m_buffer(KCheapPrepend + len), m_readerIndex(KCheapPrepend), m_writerIndex(KCheapPrepend)
        {
            ::memcpy(begin() + KCheapPrepend, readerBuf, len);
            m_writerIndex += len;
        }

        size_t readableBytes() const
        {
            return m_writerIndex - m_readerIndex;
        }

        size_t writableBytes() const
        {
            return m_buffer.size() - m_writerIndex;
        }

        size_t prependableBytes() const
        {
            return m_readerIndex;
        }

        // 返回缓冲区中可读数据的起始地址
        const char *peek() const
        {
            return begin() + m_readerIndex;
        }

        // onMessage string <- Buffer
        void retrieve(size_t len)
        {
            if (len < readableBytes())
            {
                m_readerIndex += len; // 应用只读取了刻度缓冲区数据的一部分，就是len，还剩下的m_readerIndex+=len-》m_writerIndex
            }
            else
            {
                retrieveAll();
            }
        }

        void retrieveAll()
        {
            m_readerIndex = m_writerIndex = KCheapPrepend;
        }

        // 把onMessage函数上报的Buffer数据，转成string类型的数据返回
        std::string retrieveAllAsString()
        {
            return retrieveAsString(readableBytes());
        }

        std::string retrieveAsString(size_t len)
        {
            std::string result(peek(), len);
            retrieve(len); // 上面一句把缓冲区中可读可读的数据，已经读取出来，这里肯定要对缓冲区进行复位操作
            return result;
        }

        // 需要检查 readableBytes() >= sizeof(type)
        int8_t readInt8()
        {
            assert(readableBytes() >= sizeof(int8_t));
            int8_t result = *peek();
            retrieve(sizeof(int8_t));
            return result;
        }
        uint8_t readUint8()
        {
            assert(readableBytes() >= sizeof(uint8_t));
            uint8_t result = *peek();
            retrieve(sizeof(uint8_t));
            return result;
        }

#define XX(type)                             \
    assert(readableBytes() >= sizeof(type)); \
    type result = 0;                         \
    memcpy(&result, peek(), sizeof(type));   \
    retrieve(sizeof(type));                  \
    return byteswapOnLittleEndian(result);

        int16_t readInt16()
        {
            XX(int16_t);
        }
        uint16_t readUint16()
        {
            XX(uint16_t);
        }

        int32_t readInt32()
        {
            XX(int32_t);
        }
        uint32_t readUint32()
        {
            XX(uint32_t);
        }

        int64_t readInt64()
        {
            XX(int64_t);
        }
        uint64_t readUint64()
        {
            XX(uint64_t);
        }
#undef XX

        std::string readString(size_t len)
        {
            return retrieveAsString(len);
        }

        std::string readStringForInt()
        {
            uint32_t len = readUint32();
            return retrieveAsString(len);
        }

#define XX(type)                             \
    assert(readableBytes() >= sizeof(type)); \
    type result = 0;                         \
    memcpy(&result, peek(), sizeof(type));   \
    return byteswapOnLittleEndian(result);

        int64_t peekInt64() const
        {
            XX(int64_t);
        }
        uint64_t peekUint64() const
        {
            XX(uint64_t);
        }

        int32_t peekInt32() const
        {
            XX(int32_t);
        }
        uint32_t peekUint32() const
        {
            XX(uint32_t);
        }

        int16_t peekInt16() const
        {
            XX(int16_t);
        }
        uint16_t peekUint16() const
        {
            XX(uint16_t);
        }
#undef XX

        int8_t peekInt8() const
        {
            assert(readableBytes() >= sizeof(int8_t));
            int8_t x = *peek();
            return x;
        }
        uint8_t peekUint8() const
        {
            assert(readableBytes() >= sizeof(uint8_t));
            uint8_t x = *peek();
            return x;
        }

        // m_buffer.size() - m_writerIndex    len  确保可写缓冲区能容纳可写数据
        void ensureWriteableBytes(size_t len)
        {
            if (writableBytes() < len)
            {
                makeSpace(len); // 扩容函数
            }
        }

        // 把【data，data+len】的数据添加到writable缓冲区当中
        void append(const char *data, size_t len)
        {
            ensureWriteableBytes(len);
            std::copy(data, data + len, beginWrite());
            m_writerIndex += len;
        }
        void append(const void *data, size_t len)
        {
            append((const char *)data, len);
        }

        void writeInt8(int8_t v)
        {
            append(&v, sizeof(v));
        }
        void writeUint8(uint8_t v)
        {
            append(&v, sizeof(v));
        }
#define XX(typename, type)                   \
    void write##typename(type v)             \
    {                                        \
        type vb = byteswapOnLittleEndian(v); \
        append(&vb, sizeof(vb));             \
    }
        // XX(Uint8, uint8_t)
        XX(Uint16, uint16_t)
        XX(Uint32, uint32_t)
        XX(Uint64, uint64_t)
        // XX(Int8, int8_t)
        XX(Int16, int16_t)
        XX(Int32, int32_t)
        XX(Int64, int64_t)
#undef XX

        void writeString(const std::string &v)
        {
            append(v.data(), v.size());
        }

        void writeStringForInt(const std::string &v)
        {
            writeUint32(v.size());
            writeString(v);
        }

        char *beginWrite()
        {
            return begin() + m_writerIndex;
        }

        const char *beginWrite() const
        {
            return begin() + m_writerIndex;
        }

        // 从fd上读取数据
        ssize_t readFd(int fd, int *savaErrno);
        // 通过fd发送数据
        ssize_t writeFd(int fd, int *saveErrno);

    private:
        char *begin()
        {
            return &*m_buffer.begin();
        }
        const char *begin() const
        {
            return &*m_buffer.begin();
        }

        void makeSpace(size_t len)
        {
            // 可写空间大小 + readerIndex < len + KCheapPrepend
            if (writableBytes() + prependableBytes() < len + KCheapPrepend)
            {
                m_buffer.resize(m_writerIndex + len);
            }
            else
            {
                size_t readable = readableBytes();
                std::copy(begin() + m_readerIndex, begin() + m_writerIndex, begin() + KCheapPrepend);
                m_readerIndex = KCheapPrepend;
                m_writerIndex = m_readerIndex + readable;
            }
        }

        std::vector<char> m_buffer;
        size_t m_readerIndex;
        size_t m_writerIndex;
    };

}
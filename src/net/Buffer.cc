#include "net/Buffer.h"

#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>

namespace net
{

    ssize_t Buffer::readFd(int fd, int *savaErrno)
    {
        char extrabuf[65536] = {0}; // 栈内存空间
        struct iovec vec[2];
        const size_t writable = writableBytes(); // 这是Buffer底层缓冲区剩余的可写空间大小
        vec[0].iov_base = begin() + m_writerIndex;
        vec[0].iov_len = writable;

        vec[1].iov_base = extrabuf;
        vec[1].iov_len = sizeof extrabuf;

        const int iovcnt = (writable < sizeof extrabuf) ? 2 : 1;
        const ssize_t n = ::readv(fd, vec, iovcnt);
        if (n < 0)
        {
            *savaErrno = errno;
        }
        else if ((size_t)n <= writable)
        { // buffer可写缓冲区已经够存储读出来的数据了
            m_writerIndex += n;
        }
        else
        { // extrabuf 里面也写入了数据
            m_writerIndex = m_buffer.size();
            append(extrabuf, n - writable); // m_writerIndex 开始写n-writable大小的数据
        }
        return n;
    }

    ssize_t Buffer::writeFd(int fd, int *saveErrno)
    {
        ssize_t n = ::write(fd, peek(), readableBytes());
        if (n < 0)
        {
            *saveErrno = errno;
        }
        return n;
    }

}
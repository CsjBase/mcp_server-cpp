#pragma once

// #include "net/TcpConnection.h"
//  #include "Timestamp.h"
//  #include "Buffer.h"

#include <functional>
#include <memory>

namespace net
{
    class TcpConnection;
    class Timestamp;
    class Buffer;
    using TimerCallback = std::function<void()>;
    using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;
    // using ConnectionCallback=std::function<void(const TcpConnection::ptr&)> ;
    using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;
    using WriteCompleteCallback = std::function<void(const std::shared_ptr<TcpConnection> &)>;
    using HighWaterMarkCallback = std::function<void(const std::shared_ptr<TcpConnection> &, size_t)>;

    using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection> &, Buffer *, Timestamp)>;
    // using MessageCallback1=std::function<void(const TcpConnection::ptr&, Buffer*, Timestamp)> ;

    void defaultConnectionCallback(const std::shared_ptr<TcpConnection> &conn);
    void defaultMessageCallback(const std::shared_ptr<TcpConnection> &conn, Buffer *buffer, Timestamp receiveTime);
}

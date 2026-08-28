#pragma once

#include "net/base/Socket.h"
#include "net/base/Timestamp.h"

#include <functional>
#include <string>
#include <memory>

namespace net
{
    class EventLoop;

    class Channel
    {
    public:
        typedef std::unique_ptr<Channel> ptr;
        typedef std::function<void()> EventCallback;
        typedef std::function<void(Timestamp)> ReadEventCallback;

        Channel(EventLoop *loop, int fd);
        ~Channel();
        Channel(const Channel &) = delete;
        Channel &operator=(const Channel &) = delete;

        void handleEvent(Timestamp receiveTime);
        void setReadCallback(ReadEventCallback cb)
        {
            m_readCallback = std::move(cb);
        }
        void setWriteCallback(EventCallback cb)
        {
            m_writeCallback = std::move(cb);
        }
        void setCloseCallback(EventCallback cb)
        {
            m_closeCallback = std::move(cb);
        }
        void setErrorCallback(EventCallback cb)
        {
            m_errorCallback = std::move(cb);
        }

        // 防止当channel触发事件处理时，回调任务绑定的对象（如TcpConnection）已销毁，channel还在执行TcpConnection的回调操作
        void tie(const std::shared_ptr<void> &);

        int fd() const { return m_fd; }
        int events() const { return m_events; }
        void set_revents(int revt) { m_revents = revt; } // used by pollers

        // 设置fd相应的事件状态
        void enableReading()
        {
            m_events |= kReadEvent;
            update();
        }
        void disableReading()
        {
            m_events &= ~kReadEvent;
            update();
        }
        void enableWriting()
        {
            m_events |= kWriteEvent;
            update();
        }
        void disableWriting()
        {
            m_events &= ~kWriteEvent;
            update();
        }
        void disableAll()
        {
            m_events = kNoneEvent;
            update();
        }

        // 返回fd当前的事件状态
        bool isNoneEvent() const { return m_events == kNoneEvent; }
        bool isReading() const { return m_events & kReadEvent; }
        bool isWriting() const { return m_events & kWriteEvent; }

        // channel中的m_index成员表示channel在poller的状态
        // channel未添加到poller中
        static const int kNew = -1;
        // channel已添加到poller中
        static const int kAdded = 1;
        // channel从poller中删除
        static const int kDeleted = 2;
        int index() { return m_index; }
        void setIndex(int idx) { m_index = idx; }

        EventLoop *ownerLoop() { return m_loop; }
        void remove();

    private:
        void update();
        void handleEventWithGuard(Timestamp receiveTime);

        static const int kNoneEvent;
        static const int kReadEvent;
        static const int kWriteEvent;

        EventLoop *m_loop;
        int m_fd;
        int m_events;
        int m_revents;
        int m_index;

        std::weak_ptr<void> m_tie;
        bool m_tied;

        // channel通道里面能够获知fd最终发生的具体的事件revents，所以负责具体事件的回调操作
        ReadEventCallback m_readCallback;
        EventCallback m_writeCallback;
        EventCallback m_closeCallback;
        EventCallback m_errorCallback;
    };

}
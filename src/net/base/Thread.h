#pragma once

#include <functional>
#include <memory>
#include <thread>
#include <string>
#include <atomic>

namespace net
{
    class Thread
    {
    public:
        using ThreadFunc = std::function<void()>;

        explicit Thread(ThreadFunc, const std::string &name = std::string());

        Thread(const Thread &) = delete;
        Thread &operator=(const Thread &) = delete;

        ~Thread();

        void start();
        void join();

        bool started() const { return m_started; }
        pid_t tid() const { return m_tid; }
        const std::string &name() const { return m_name; }

        static int numCreated() { return s_numCreated.load(); }

    private:
        void setDefaultName();

        bool m_started;
        bool m_joined;
        std::unique_ptr<std::thread> m_thread;
        pid_t m_tid;
        ThreadFunc m_func;
        std::string m_name;
        static std::atomic_int s_numCreated;
    };
}
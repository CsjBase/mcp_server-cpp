#pragma once

#include "net/base/Timestamp.h"
#include "net/CallBacks.h"

#include <atomic>

namespace net
{
    class Timer
    {
    public:
        typedef std::shared_ptr<Timer> ptr;
        Timer(TimerCallback cb, Timestamp when, double interval)
            : m_callback(std::move(cb)),
              m_expiration(when),
              m_interval(interval),
              m_repeat(interval > 0.0),
              m_sequence(s_numCreated.fetch_add(1) + 1)
        {
        }

        void run() const
        {
            m_callback();
        }

        Timestamp expiration() const
        {
            return m_expiration;
        }

        bool repeat() const
        {
            return m_repeat;
        }

        int64_t sequence() const
        {
            return m_sequence;
        }

        void restart(Timestamp now);

        static int64_t numCreated()
        {
            return s_numCreated.load();
        }

    private:
        const TimerCallback m_callback;
        Timestamp m_expiration;
        const double m_interval;
        const bool m_repeat;
        const int64_t m_sequence;

        static std::atomic<int64_t> s_numCreated;
    };
}
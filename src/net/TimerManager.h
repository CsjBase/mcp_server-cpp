#pragma once

#include "net/base/Timestamp.h"
#include "net/CallBacks.h"
#include "net/Channel.h"
#include "net/Timer.h"
#include "net/TimerId.h"

#include <set>
#include <vector>
#include <mutex>

namespace net
{
    class EventLoop;

    class TimerManager
    {
    public:
        explicit TimerManager(EventLoop *loop);
        ~TimerManager();

        TimerManager(const TimerManager &) = delete;
        TimerManager &operator=(const TimerManager &) = delete;

        ///
        /// Schedules the callback to be run at given time,
        /// repeats if @c interval > 0.0.
        ///
        /// Must be thread safe. Usually be called from other threads.
        TimerId addTimer(TimerCallback cb,
                         Timestamp when,
                         double interval);

        void cancel(TimerId timerId);

    private:
        using Entry = std::pair<Timestamp, std::unique_ptr<Timer>>;
        using RawEntry = std::pair<Timestamp, Timer *>;
        struct EntryCompare
        {
            using is_transparent = std::true_type;

            bool operator()(const Entry &lhs, const Entry &rhs) const
            {
                if (lhs.first != rhs.first)
                {
                    return lhs.first < rhs.first;
                }
                else
                {
                    return lhs.second.get() < rhs.second.get();
                }
            }

            bool operator()(const Entry &lhs, const RawEntry &rhs) const
            {
                if (lhs.first != rhs.first)
                {
                    return lhs.first < rhs.first;
                }
                else
                {
                    return lhs.second.get() < rhs.second;
                }
            }

            bool operator()(const RawEntry &lhs, const Entry &rhs) const
            {
                if (lhs.first != rhs.first)
                {
                    return lhs.first < rhs.first;
                }
                else
                {
                    return lhs.second < rhs.second.get();
                }
            }
        };

        using TimerList = std::set<Entry, EntryCompare>;
        using ActiveTimer = std::pair<Timer *, int64_t>;
        using ActiveTimerSet = std::set<ActiveTimer>;

        void addTimerInLoop(std::unique_ptr<Timer> timer);
        void cancelInLoop(TimerId timerId);
        // called when timerfd alarms
        void handleRead();
        // move out all expired timers
        std::vector<Entry> getExpired(Timestamp now);
        void reset(std::vector<Entry> &expired, Timestamp now);

        bool insert(std::unique_ptr<Timer> timer);

    private:
        EventLoop *loop_;
        const int timerfd_;
        Channel timerfdChannel_;
        // Timer list sorted by expiration
        TimerList timers_;

        // for cancel()
        ActiveTimerSet activeTimers_;
        bool callingExpiredTimers_; /* atomic */
        ActiveTimerSet cancelingTimers_;
    };

}
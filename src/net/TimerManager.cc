#include "net/TimerManager.h"
#include "logger/log.h"
#include "net/EventLoop.h"

#include <sys/timerfd.h>
#include <assert.h>

namespace net
{
    static int createTimerfd()
    {
        int timerfd = ::timerfd_create(CLOCK_MONOTONIC,
                                       TFD_NONBLOCK | TFD_CLOEXEC);
        if (timerfd < 0)
        {
            LOG_FATAL(LOGGER_DEFAULT(), "Failed in timerfd_create");
        }
        return timerfd;
    }

    static struct timespec howMuchTimeFromNow(Timestamp when)
    {
        int64_t microseconds = when.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();
        if (microseconds < 100)
        {
            microseconds = 100;
        }
        struct timespec ts;
        ts.tv_sec = static_cast<time_t>(
            microseconds / Timestamp::kMicroSecondsPerSecond);
        ts.tv_nsec = static_cast<long>(
            (microseconds % Timestamp::kMicroSecondsPerSecond) * 1000);
        return ts;
    }

    static void resetTimerfd(int timerfd, Timestamp expiration)
    {
        // wake up loop by timerfd_settime()
        struct itimerspec newValue;
        struct itimerspec oldValue;
        memset(&newValue, 0, sizeof newValue);
        memset(&oldValue, 0, sizeof oldValue);
        newValue.it_value = howMuchTimeFromNow(expiration);
        int ret = ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
        if (ret)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "timerfd_settime()");
        }
    }

    static void readTimerfd(int timerfd, Timestamp now)
    {
        uint64_t howmany;
        ssize_t n = ::read(timerfd, &howmany, sizeof howmany);
        LOG_TRACE(LOGGER_DEFAULT(), "TimerManager::handleRead() {} at {}", howmany, now.toString());
        if (n != sizeof howmany)
        {
            LOG_ERROR(LOGGER_DEFAULT(), "TimerManager::handleRead() reads {} bytes instead of 8", n);
        }
    }

    TimerManager::TimerManager(EventLoop *loop)
        : loop_(loop),
          timerfd_(createTimerfd()),
          timerfdChannel_(loop, timerfd_),
          timers_(),
          callingExpiredTimers_(false)
    {
        timerfdChannel_.setReadCallback(
            std::bind(&TimerManager::handleRead, this));
        // we are always reading the timerfd, we disarm it with timerfd_settime.
        timerfdChannel_.enableReading();
    }

    TimerManager::~TimerManager()
    {
        timerfdChannel_.disableAll();
        timerfdChannel_.remove();
        ::close(timerfd_);
        // do not remove channel, since we're in EventLoop::dtor();
        // for (const Entry &timer : timers_)
        // {
        //     delete timer.second;
        // }
    }

    TimerId TimerManager::addTimer(TimerCallback cb,
                                   Timestamp when,
                                   double interval)
    {
        std::unique_ptr<Timer> timer = std::make_unique<Timer>(std::move(cb), when, interval);
        TimerId timerId(timer.get(), timer->sequence());
        // loop_->runInLoop(
        //     std::bind(&TimerManager::addTimerInLoop, this, timer));

        std::function<void()> functor = [this, timer = std::make_shared<decltype(timer)>(std::move(timer))]() mutable
        {
            addTimerInLoop(std::move(*timer));
        };
        loop_->runInLoop(std::move(functor));
        return timerId;
    }

    void TimerManager::cancel(TimerId timerId)
    {
        loop_->runInLoop(
            std::bind(&TimerManager::cancelInLoop, this, timerId));
    }

    void TimerManager::addTimerInLoop(std::unique_ptr<Timer> timer)
    {
        loop_->assertInLoopThread();
        Timestamp expiration = timer->expiration();
        bool earliestChanged = insert(std::move(timer));

        if (earliestChanged)
        {
            resetTimerfd(timerfd_, expiration);
        }
    }

    void TimerManager::cancelInLoop(TimerId timerId)
    {
        loop_->assertInLoopThread();
        assert(timers_.size() == activeTimers_.size());
        ActiveTimer timer(timerId.timer_, timerId.sequence_);
        ActiveTimerSet::iterator it = activeTimers_.find(timer);
        if (it != activeTimers_.end())
        {
            RawEntry entry(it->first->expiration(), it->first);
            activeTimers_.erase(it);
            auto erase_it = timers_.find(entry);
            erase_it = timers_.erase(erase_it);
            assert(erase_it != timers_.end());
            // assert(n == 1);
            // (void)n;
            // delete it->first; // FIXME: no delete please //use unique_ptr
        }
        else if (callingExpiredTimers_)
        {
            cancelingTimers_.insert(timer);
        }
        assert(timers_.size() == activeTimers_.size());
    }

    void TimerManager::handleRead()
    {
        loop_->assertInLoopThread();
        Timestamp now(Timestamp::now());
        readTimerfd(timerfd_, now);

        std::vector<Entry> expired = getExpired(now);

        callingExpiredTimers_ = true;
        cancelingTimers_.clear();
        // safe to callback outside critical section
        for (const Entry &it : expired)
        {
            it.second->run();
        }
        callingExpiredTimers_ = false;

        reset(expired, now);
    }

    std::vector<TimerManager::Entry> TimerManager::getExpired(Timestamp now)
    {
        assert(timers_.size() == activeTimers_.size());
        std::vector<Entry> expired;
        Entry sentry(Timestamp(now.microSecondsSinceEpoch() + 1), nullptr);
        TimerList::iterator end = timers_.lower_bound(sentry);
        assert(end == timers_.end() || now < end->first);
        // std::copy(timers_.begin(), end, back_inserter(expired));
        // timers_.erase(timers_.begin(), end);
        for (auto it = timers_.begin(); it != end;)
        {
            auto nh = timers_.extract(it++); // 提取节点，it 先递增到下一个有效位置
            expired.emplace_back(std::move(nh.value()));
        }

        for (const Entry &it : expired)
        {
            ActiveTimer timer(it.second.get(), it.second->sequence());
            size_t n = activeTimers_.erase(timer);
            assert(n == 1);
            (void)n;
        }

        assert(timers_.size() == activeTimers_.size());
        return expired;
    }

    void TimerManager::reset(std::vector<Entry> &expired, Timestamp now)
    {
        Timestamp nextExpire;

        for (Entry &it : expired)
        {
            ActiveTimer timer(it.second.get(), it.second->sequence());
            if (it.second->repeat() && cancelingTimers_.find(timer) == cancelingTimers_.end())
            {
                it.second->restart(now);
                insert(std::move(it.second));
            }
            else
            {
                // FIXME move to a free list
                // delete it.second; // FIXME: no delete please
            }
        }

        if (!timers_.empty())
        {
            nextExpire = timers_.begin()->second->expiration();
        }

        if (nextExpire.valid())
        {
            resetTimerfd(timerfd_, nextExpire);
        }
    }

    bool TimerManager::insert(std::unique_ptr<Timer> timer)
    {
        loop_->assertInLoopThread();
        assert(timers_.size() == activeTimers_.size());
        bool earliestChanged = false;
        Timestamp when = timer->expiration();
        TimerList::iterator it = timers_.begin();
        if (it == timers_.end() || when < it->first)
        {
            earliestChanged = true;
        }
        Timer *rawTimer = timer.get();
        {
            std::pair<ActiveTimerSet::iterator, bool> result = activeTimers_.insert(ActiveTimer(rawTimer, timer->sequence()));
            assert(result.second);
            (void)result;
        }
        {
            std::pair<TimerList::iterator, bool> result = timers_.insert(Entry(when, std::move(timer)));
            assert(result.second);
            (void)result;
        }

        assert(timers_.size() == activeTimers_.size());
        return earliestChanged;
    }

}
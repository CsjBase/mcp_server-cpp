#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <iostream>

#include "circular_q.h"

namespace utils
{

    template <typename T>
    class mpmc_blocking_queue
    {
    public:
        mpmc_blocking_queue() = default;
        explicit mpmc_blocking_queue(size_t max_items)
            : queue_(max_items) {}

        mpmc_blocking_queue(const mpmc_blocking_queue &) = delete;
        mpmc_blocking_queue &operator=(const mpmc_blocking_queue &) = delete;
        mpmc_blocking_queue(mpmc_blocking_queue &&) = delete;
        mpmc_blocking_queue &operator=(mpmc_blocking_queue &&) = delete;

        /** 阻塞直到有空闲位置或中断，入队成功返回 true，中断返回 false */
        bool enqueue(const T &item);
        bool enqueue(T &&item);

        /** 非阻塞入队：若队列已满则覆盖最旧消息入队，入队成功返回 true，中断返回 false*/
        bool enqueue_overwrite(const T &item);
        bool enqueue_overwrite(T &&item);

        /** 非阻塞入队，若满或中断则立即返回 false */
        bool try_enqueue(const T &item);
        bool try_enqueue(T &&item);

        /** 阻塞直到队列不空或中断。中断时根据中断模式设置返回值*/
        std::optional<T> dequeue();

        /** 带超时的阻塞出队。 */
        template <typename Rep, typename Period>
        std::optional<T> dequeue_for(const std::chrono::duration<Rep, Period> &timeout);

        /** 非阻塞出队，若空则返回 std::nullopt */
        std::optional<T> try_dequeue();

        /**
         * 优雅停止：允许 dequeue 操作排空队列中的剩余元素后再返回 nullopt
         * 停止后不允许新的入队操作
         */
        void stop_gracefully();

        /**
         * 立即停止：dequeue 操作立即返回 nullopt，丢弃队列中的剩余元素
         * 停止后不允许新的入队操作
         */
        void stop_immediately();

        size_t size() const;

        size_t capacity() const;

        bool empty() const;

        bool full() const;

        size_t overrun_counter() const;

        void reset_overrun_counter();

        size_t discard_counter() const;

        void reset_discard_counter();

    private:
        enum class State : uint8_t
        {
            None = 0, // 正常运行
            Drain,    // 优雅中断：dequeue 排空队列后才返回 nullopt；不允许入队
            Immediate // 立即中断：dequeue/enqueue 立即返回 nullopt/false，跳过排空；不允许入队
        };
        bool is_stopped_() const
        {
            return state_.load(std::memory_order_acquire) != State::None;
        }

        circular_q<T> queue_;
        mutable std::mutex mutex_;
        std::condition_variable not_full_cv_;
        std::condition_variable not_empty_cv_;
        std::atomic<size_t> discard_counter_{0};
        std::atomic<State> state_{State::None};
    };

    // ---- mpmc_blocking_queue 实现 ----

    template <typename T>
    bool mpmc_blocking_queue<T>::enqueue(const T &item)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_full_cv_.wait(lock, [this]
                              { return !queue_.full() || is_stopped_(); });
            if (is_stopped_())
                return false;
            queue_.push_back(T(item));
        }
        not_empty_cv_.notify_one();
        return true;
    }

    template <typename T>
    bool mpmc_blocking_queue<T>::enqueue(T &&item)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_full_cv_.wait(lock, [this]
                              { return !queue_.full() || is_stopped_(); });
            if (is_stopped_())
                return false;
            queue_.push_back(std::move(item));
        }
        not_empty_cv_.notify_one();
        return true;
    }

    template <typename T>
    bool mpmc_blocking_queue<T>::enqueue_overwrite(const T &item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_stopped_())
                return false;
            queue_.push_back(T(item));
        }
        not_empty_cv_.notify_one();
        return true;
    }

    template <typename T>
    bool mpmc_blocking_queue<T>::enqueue_overwrite(T &&item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_stopped_())
                return false;
            queue_.push_back(std::move(item));
        }
        not_empty_cv_.notify_one();
        return true;
    }

    template <typename T>
    bool mpmc_blocking_queue<T>::try_enqueue(const T &item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.full() || is_stopped_())
            {
                if (queue_.full())
                    discard_counter_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            queue_.push_back(T(item));
        }
        not_empty_cv_.notify_one();
        return true;
    }

    template <typename T>
    bool mpmc_blocking_queue<T>::try_enqueue(T &&item)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.full() || is_stopped_())
            {
                if (queue_.full())
                    discard_counter_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            queue_.push_back(std::move(item));
        }
        not_empty_cv_.notify_one();
        return true;
    }

    template <typename T>
    std::optional<T> mpmc_blocking_queue<T>::dequeue()
    {
        std::optional<T> result;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_cv_.wait(lock, [this]
                               { return !queue_.empty() || is_stopped_(); });

            auto mode = state_.load(std::memory_order_acquire);
            if (mode != State::Immediate && !queue_.empty())
            {
                result = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        if (result)
        {
            not_full_cv_.notify_one();
        }
        return result;
    }

    template <typename T>
    template <typename Rep, typename Period>
    std::optional<T> mpmc_blocking_queue<T>::dequeue_for(
        const std::chrono::duration<Rep, Period> &timeout)
    {
        std::optional<T> result;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            not_empty_cv_.wait_for(lock, timeout, [this]
                                   { return !queue_.empty() || is_stopped_(); });

            auto mode = state_.load(std::memory_order_acquire);
            if (mode != State::Immediate && !queue_.empty())
            {
                result = std::move(queue_.front());
                queue_.pop_front();
            }
        }
        not_full_cv_.notify_one();
        return result;
    }

    template <typename T>
    std::optional<T> mpmc_blocking_queue<T>::try_dequeue()
    {
        std::optional<T> result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.empty() || is_stopped_())
                return std::nullopt;
            result = std::move(queue_.front());
            queue_.pop_front();
        }
        not_full_cv_.notify_one();
        return result;
    }

    template <typename T>
    void mpmc_blocking_queue<T>::stop_gracefully()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_stopped_())
                return;
            state_.store(State::Drain, std::memory_order_release);
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    template <typename T>
    void mpmc_blocking_queue<T>::stop_immediately()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (is_stopped_())
                return;
            state_.store(State::Immediate, std::memory_order_release);
        }
        not_empty_cv_.notify_all();
        not_full_cv_.notify_all();
    }

    template <typename T>
    size_t mpmc_blocking_queue<T>::size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    template <typename T>
    size_t mpmc_blocking_queue<T>::capacity() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.capacity();
    }

    template <typename T>
    bool mpmc_blocking_queue<T>::empty() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    template <typename T>
    bool mpmc_blocking_queue<T>::full() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.full();
    }

    template <typename T>
    size_t mpmc_blocking_queue<T>::overrun_counter() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.overrun_counter();
    }

    template <typename T>
    void mpmc_blocking_queue<T>::reset_overrun_counter()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.reset_overrun_counter();
    }

    template <typename T>
    size_t mpmc_blocking_queue<T>::discard_counter() const
    {
        return discard_counter_.load(std::memory_order_relaxed);
    }

    template <typename T>
    void mpmc_blocking_queue<T>::reset_discard_counter()
    {
        discard_counter_.store(0, std::memory_order_relaxed);
    }

} // namespace utils

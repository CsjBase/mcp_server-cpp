#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

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

        /** 阻塞直到有空闲位置，然后入队 */
        void enqueue(const T &item)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                not_full_cv_.wait(lock, [this]
                                  { return !queue_.full(); });
                queue_.push_back(T(item));
            }
            not_empty_cv_.notify_one();
        }

        void enqueue(T &&item)
        {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                not_full_cv_.wait(lock, [this]
                                  { return !queue_.full(); });
                queue_.push_back(std::move(item));
            }
            not_empty_cv_.notify_one();
        }

        /** 非阻塞入队：若队列已满则覆盖最旧消息 */
        void enqueue_overwrite(const T &item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push_back(T(item));
            }
            not_empty_cv_.notify_one();
        }

        void enqueue_overwrite(T &&item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queue_.push_back(std::move(item));
            }
            not_empty_cv_.notify_one();
        }

        /** 尝试入队，若满则立即返回 false */
        bool try_enqueue(const T &item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.full())
                {
                    discard_counter_.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
                queue_.push_back(T(item));
            }
            not_empty_cv_.notify_one();
            return true;
        }

        bool try_enqueue(T &&item)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.full())
                {
                    discard_counter_.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }
                queue_.push_back(std::move(item));
            }
            not_empty_cv_.notify_one();
            return true;
        }

        /** 阻塞直到队列不为空，弹出并返回消息 */
        T dequeue()
        {
            T item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                not_empty_cv_.wait(lock, [this]
                                   { return !queue_.empty(); });
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            not_full_cv_.notify_one();
            return item;
        }

        /** 带超时的阻塞出队，超时返回 std::nullopt */
        template <typename Rep, typename Period>
        std::optional<T> dequeue_for(const std::chrono::duration<Rep, Period> &timeout)
        {
            std::optional<T> result;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (!not_empty_cv_.wait_for(lock, timeout, [this]
                                            { return !queue_.empty(); }))
                {
                    return std::nullopt;
                }
                result = std::move(queue_.front());
                queue_.pop_front();
            }
            not_full_cv_.notify_one();
            return result;
        }

        /** 非阻塞出队，若空则返回 std::nullopt */
        std::optional<T> try_dequeue()
        {
            std::optional<T> result;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.empty())
                {
                    return std::nullopt;
                }
                result = std::move(queue_.front());
                queue_.pop_front();
            }
            not_full_cv_.notify_one();
            return result;
        }

        size_t size() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.size();
        }

        bool empty() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.empty();
        }

        bool full() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.full();
        }

        size_t overrun_counter() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return queue_.overrun_counter();
        }

        void reset_overrun_counter()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.reset_overrun_counter();
        }

        /** 入队失败（try_enqueue 队列满）丢弃元素计数 */
        size_t discard_counter() const
        {
            return discard_counter_.load(std::memory_order_relaxed);
        }

        void reset_discard_counter()
        {
            discard_counter_.store(0, std::memory_order_relaxed);
        }

    private:
        circular_q<T> queue_;
        mutable std::mutex mutex_;
        std::condition_variable not_full_cv_;
        std::condition_variable not_empty_cv_;
        std::atomic<size_t> discard_counter_{0};
    };

} // namespace utils

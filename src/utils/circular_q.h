#pragma once

#include <vector>

namespace utils
{

    template <typename T>
    class circular_q
    {
    public:
        circular_q() = default;
        explicit circular_q(size_t max_items);

        circular_q(const circular_q &) = default;
        circular_q &operator=(const circular_q &) = default;
        circular_q(circular_q &&other) noexcept;

        void push_back(T &&item);
        const T &front() const;
        T &front();

        void pop_front();

        bool empty() const;
        bool full() const;

        size_t size() const;
        size_t overrun_counter() const noexcept { return overrun_counter_; }
        void reset_overrun_counter() { overrun_counter_ = 0; }

    private:
        size_t max_items_ = 0;
        size_t head_ = 0;
        size_t tail_ = 0;
        size_t overrun_counter_ = 0;
        std::vector<T> items_;
    };

    template <typename T>
    circular_q<T>::circular_q(size_t max_items)
        : max_items_(max_items + 1), items_(max_items_) // 多一个格子好判断empty或full
    {
    }

    template <typename T>
    void circular_q<T>::push_back(T &&item)
    {
        if (max_items_ > 0)
        {
            items_[tail_] = std::move(item);
            tail_ = (tail_ + 1) % max_items_;
            if (tail_ == head_)
            {
                head_ = (head_ + 1) % max_items_;
                ++overrun_counter_;
            }
        }
    }

    template <typename T>
    const T &circular_q<T>::front() const
    {
        return items_[head_];
    }

    template <typename T>
    T &circular_q<T>::front()
    {
        return items_[head_];
    }

    template <typename T>
    void circular_q<T>::pop_front()
    {
        head_ = (head_ + 1) % max_items_;
    }

    template <typename T>
    bool circular_q<T>::empty() const
    {
        return head_ == tail_;
    }

    template <typename T>
    bool circular_q<T>::full() const
    {
        if (max_items_ > 0)
        {
            return (tail_ + 1) % max_items_ == head_;
        }
        return false;
    }

    template <typename T>
    size_t circular_q<T>::size() const
    {
        if (tail_ >= head_)
        {
            return tail_ - head_;
        }
        else
        {
            return max_items_ - head_ + tail_;
        }
    }
}
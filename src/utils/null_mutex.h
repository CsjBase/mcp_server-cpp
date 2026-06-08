#pragma once

#include <atomic>

namespace utils
{
    class null_mutex
    {
    public:
        void lock() {}
        void unlock() {}
    };

    template <typename T>
    class null_atomic
    {
    public:
        null_atomic() = default;
        explicit null_atomic(T new_value)
            : value_(new_value) {}

        T load(std::memory_order = std::memory_order_relaxed) const { return value_; }
        void store(T value, std::memory_order = std::memory_order_relaxed) { value_ = value; }
        T exchange(T value, std::memory_order = std::memory_order_relaxed)
        {
            T old = value_;
            value_ = value;
            return old;
        }
        T fetch_add(T value, std::memory_order = std::memory_order_relaxed)
        {
            T old = value_;
            value_ += value;
            return old;
        }
        T fetch_sub(T value, std::memory_order = std::memory_order_relaxed)
        {
            T old = value_;
            value_ -= value;
            return old;
        }
        T fetch_and(T value, std::memory_order = std::memory_order_relaxed)
        {
            T old = value_;
            value_ &= value;
            return old;
        }

    private:
        T value_{};
    };
}
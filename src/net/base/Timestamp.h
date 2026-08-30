#pragma once

#include <stdint.h>
#include <string>

namespace net
{
    class Timestamp
    {
    public:
        static constexpr int kMicroSecondsPerSecond = 1000 * 1000;
        Timestamp();
        Timestamp(int64_t microSecondsSinceEpoch);

        static Timestamp now();
        std::string toString() const;

        int64_t toSeconds();
        static Timestamp addSeconds(Timestamp timestamp, double seconds);
        static Timestamp invalid()
        {
            return Timestamp();
        }
        bool valid() const { return microSecondsSinceEpoch_ > 0; }

        bool operator<(Timestamp lhs) const
        {
            return this->microSecondsSinceEpoch_ < lhs.microSecondsSinceEpoch_;
        }

        bool operator==(Timestamp lhs) const
        {
            return this->microSecondsSinceEpoch_ == lhs.microSecondsSinceEpoch_;
        }

        bool operator!=(Timestamp lhs) const
        {
            return this->microSecondsSinceEpoch_ != lhs.microSecondsSinceEpoch_;
        }

        // Timestamp &operator+=(Timestamp lhs)
        // {
        //     this->microSecondsSinceEpoch_ += lhs.microSecondsSinceEpoch_;
        //     return *this;
        // }

        // Timestamp &operator+=(int64_t lhs)
        // {
        //     this->microSecondsSinceEpoch_ += lhs;
        //     return *this;
        // }

        // Timestamp &operator-=(Timestamp lhs)
        // {
        //     this->microSecondsSinceEpoch_ -= lhs.microSecondsSinceEpoch_;
        //     return *this;
        // }

        // Timestamp &operator-=(int64_t lhs)
        // {
        //     this->microSecondsSinceEpoch_ -= lhs;
        //     return *this;
        // }

        int64_t microSecondsSinceEpoch() { return microSecondsSinceEpoch_; }

    private:
        int64_t microSecondsSinceEpoch_;
    };
}
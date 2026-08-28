#pragma once

#include <stdint.h>
#include <string>

namespace net
{
    class Timestamp
    {
    public:
        Timestamp();
        Timestamp(int64_t microSecondsSinceEpoch);

        static Timestamp now();
        std::string toString() const;

        int64_t toSeconds();

        Timestamp &operator+=(Timestamp lhs)
        {
            this->microSecondsSinceEpoch_ += lhs.microSecondsSinceEpoch_;
            return *this;
        }

        Timestamp &operator+=(int64_t lhs)
        {
            this->microSecondsSinceEpoch_ += lhs;
            return *this;
        }

        Timestamp &operator-=(Timestamp lhs)
        {
            this->microSecondsSinceEpoch_ -= lhs.microSecondsSinceEpoch_;
            return *this;
        }

        Timestamp &operator-=(int64_t lhs)
        {
            this->microSecondsSinceEpoch_ -= lhs;
            return *this;
        }

        int64_t microSecondsSinceEpoch() { return microSecondsSinceEpoch_; }

    private:
        int64_t microSecondsSinceEpoch_;
    };
}
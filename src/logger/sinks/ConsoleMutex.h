#pragma once

#include "utils/null_mutex.h"

#include <mutex>

namespace logger
{

    class ConsoleMutex
    {
    public:
        using mutex_t = std::mutex;
        static mutex_t &get()
        {
            static mutex_t mutex;
            return mutex;
        }
    };

    class ConsoleNullMutex
    {
    public:
        using mutex_t = utils::null_mutex;
        static mutex_t &get()
        {
            static mutex_t mutex;
            return mutex;
        }
    };
}
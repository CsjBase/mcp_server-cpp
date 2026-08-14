#pragma once

#include "logger/LogEvent.h"

namespace logger
{
    namespace details
    {
        class LogEventBuffer : public LogEvent
        {
            memory_buf_t buffer;
            void update_string_views();

        public:
            LogEventBuffer() = default;
            explicit LogEventBuffer(const LogEvent &orig_ev);
            LogEventBuffer(const LogEventBuffer &other);
            LogEventBuffer(LogEventBuffer &&other) noexcept;
            LogEventBuffer &operator=(const LogEventBuffer &other);
            LogEventBuffer &operator=(LogEventBuffer &&other) noexcept;
        };
    }
}
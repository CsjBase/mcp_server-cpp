
#include "logger/LogEventBuffer.h"

namespace logger
{
    namespace details
    {

        LogEventBuffer::LogEventBuffer(const LogEvent &orig_ev)
            : LogEvent{orig_ev}
        {
            buffer.append(logger_name.begin(), logger_name.end());
            buffer.append(payload.begin(), payload.end());
            update_string_views();
        }

        LogEventBuffer::LogEventBuffer(const LogEventBuffer &other)
            : LogEvent{other}
        {
            buffer.append(logger_name.begin(), logger_name.end());
            buffer.append(payload.begin(), payload.end());
            update_string_views();
        }

        LogEventBuffer::LogEventBuffer(LogEventBuffer &&other) noexcept
            : LogEvent{other},
              buffer{std::move(other.buffer)}
        {
            update_string_views();
        }

        LogEventBuffer &LogEventBuffer::operator=(const LogEventBuffer &other)
        {
            LogEvent::operator=(other);
            buffer.clear();
            buffer.append(other.buffer.data(), other.buffer.data() + other.buffer.size());
            update_string_views();
            return *this;
        }

        LogEventBuffer &LogEventBuffer::operator=(LogEventBuffer &&other) noexcept
        {
            LogEvent::operator=(other);
            buffer = std::move(other.buffer);
            update_string_views();
            return *this;
        }

        void LogEventBuffer::update_string_views()
        {
            logger_name = std::string_view{buffer.data(), logger_name.size()};
            payload = std::string_view{buffer.data() + logger_name.size(), payload.size()};
        }

    } // namespace details
} // namespace logger

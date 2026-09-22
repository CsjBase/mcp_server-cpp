#pragma once

#include <string>
#include <functional>
#include <optional>

namespace mcp
{

    using MessageHandler = std::function<
        std::optional<std::string>(const std::string &raw)>;

    class ITransport
    {
    public:
        virtual ~ITransport() = default;
        virtual void set_handler(MessageHandler handler) = 0;
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual void send(const std::string &raw) = 0;
    };

} // namespace mcp
#pragma once

#include <exception>
#include <string>

namespace config
{

    class ConfigException : public std::exception
    {
    public:
        explicit ConfigException(std::string msg);
        const char *what() const noexcept override;

    private:
        std::string msg_;
    };
}
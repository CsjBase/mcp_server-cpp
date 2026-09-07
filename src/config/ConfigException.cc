
#include "config/ConfigException.h"

namespace config
{

    ConfigException::ConfigException(std::string msg)
        : msg_(std::move(msg))
    {
    }
    const char *ConfigException::what() const noexcept
    {
        return msg_.c_str();
    }
}
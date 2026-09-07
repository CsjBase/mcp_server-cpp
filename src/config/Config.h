#pragma once

#include "config/ConfigVar.h"
#include "config/ConfigException.h"

#include <unordered_map>
#include <shared_mutex>
#include <nlohmann/json.hpp>

namespace config
{

    class Config
    {
    public:
        using ConfigVarMap = std::unordered_map<std::string, ConfigVarBase::ptr>;

        template <typename T>
        static typename ConfigVar<T>::ptr lookup(std::string name)
        {
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            std::shared_lock lock(getMutex());
            auto it = getDatas().find(name);
            if (it != getDatas().end())
            {
                auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
                if (tmp)
                {
                    return tmp;
                }
                else
                {
                    throw ConfigException("look up name: " + name + " exists but type not " + typeid(T).name() + " real type is " + it->second->getTypeName());
                }
            }
            return nullptr;
        }

        template <typename T>
        static typename ConfigVar<T>::ptr lookup(std::string name, const T &default_value, const std::string &description = "")
        {
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            {
                std::shared_lock lock(getMutex());
                auto it = getDatas().find(name);
                if (it != getDatas().end())
                {
                    auto tmp = std::dynamic_pointer_cast<ConfigVar<T>>(it->second);
                    if (tmp)
                    {
                        return tmp;
                    }
                    else
                    {
                        throw ConfigException("look up name: " + name + " exists but type not " + typeid(T).name() + " real type is " + it->second->getTypeName());
                    }
                }
            }
            std::unique_lock lock(getMutex());
            typename ConfigVar<T>::ptr var(new ConfigVar<T>(name, default_value, description));
            getDatas()[name] = var;
            return var;
        }

        static ConfigVarBase::ptr lookupBase(std::string name);

        static void loadFromJson(const nlohmann::json &json);
        static void loadFromFile(const std::string &file);
        static void loadFromConfDir(const std::string &path, bool force = false);

        static ConfigVarMap &getDatas()
        {
            static ConfigVarMap s_datas;
            return s_datas;
        }

        static std::shared_mutex &getMutex()
        {
            static std::shared_mutex s_mutex;
            return s_mutex;
        }
    };
}
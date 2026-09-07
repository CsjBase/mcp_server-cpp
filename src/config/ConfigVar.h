#pragma once

#include <string>
#include <mutex>
#include <map>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace config
{
    class ConfigVarBase
    {
    public:
        using ptr = std::shared_ptr<ConfigVarBase>;

        ConfigVarBase(const std::string &name, const std::string &description)
            : m_name(name), m_description(description)
        {
        }
        virtual ~ConfigVarBase() = default;

        const std::string &getName() const { return m_name; }
        const std::string &getDescription() const { return m_description; }

        virtual std::string getTypeName() const = 0;
        virtual void fromJson(const nlohmann::json &json) = 0;
        virtual nlohmann::json toJson() = 0;

    protected:
        std::string m_name;
        std::string m_description;
    };

    template <typename T>
    class ConfigVar : public ConfigVarBase
    {
    public:
        using ptr = std::shared_ptr<ConfigVar<T>>;
        using OnChangeCallback = std::function<void(const T &oldValue, const T &newValue)>;

        ConfigVar(const std::string &name, const T &defaultValue, const std::string &description)
            : ConfigVarBase(name, description), m_value(defaultValue)
        {
        }

        std::string getTypeName() const override
        {
            return typeid(T).name();
        }
        void fromJson(const nlohmann::json &json) override
        {
            setValue(json.get<T>());
        }
        nlohmann::json toJson() override
        {
            return m_value;
        }

        const T getValue()
        {
            std::lock_guard lock(m_mutex);
            return m_value;
        }

        void setValue(const T &v)
        {
            std::lock_guard lock(m_mutex);
            if (m_value == v)
                return;
            T oldValue = m_value;
            m_value = v;
            for (auto &it : m_cbs)
                it.second(oldValue, v);
        }

        uint64_t addChangeCallback(OnChangeCallback cb)
        {
            static uint64_t s_funId = 0;
            std::lock_guard lock(m_mutex);
            ++s_funId;
            m_cbs[s_funId] = cb;
            return s_funId;
        }

        void removeChangeCallback(uint64_t funId)
        {
            std::lock_guard lock(m_mutex);
            m_cbs.erase(funId);
        }

        void clearChangeCallbacks()
        {
            std::lock_guard lock(m_mutex);
            m_cbs.clear();
        }

    private:
        std::mutex m_mutex;
        T m_value;
        std::map<uint64_t, OnChangeCallback> m_cbs;
    };
}
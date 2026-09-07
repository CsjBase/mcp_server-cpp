#include "config/Config.h"

#include <functional>
#include <list>
#include <fstream>
#include <filesystem>

namespace config
{

    ConfigVarBase::ptr Config::lookupBase(std::string name)
    {
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        std::shared_lock lock(getMutex());
        auto it = getDatas().find(name);
        return it == getDatas().end() ? nullptr : it->second;
    }

    static void listAllNodes(const std::string &prefix, const nlohmann::json &json, std::list<std::pair<std::string, nlohmann::json>> &nodes)
    {
        if (prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789") != std::string::npos)
        {
            throw ConfigException("invalid config name: " + prefix + ": " + json.dump());
        }
        nodes.emplace_back(prefix, json);

        if (json.is_object())
        {
            for (auto it = json.begin(); it != json.end(); ++it)
            {
                listAllNodes(prefix.empty() ? it.key() : (prefix + "." + it.key()), it.value(), nodes);
            }
        }
        else if (json.is_array())
        {
            for (size_t i = 0; i < json.size(); ++i)
            {
                listAllNodes(prefix.empty() ? std::to_string(i) : (prefix + "." + std::to_string(i)), json[i], nodes);
            }
        }
    }

    void Config::loadFromJson(const nlohmann::json &json)
    {
        std::list<std::pair<std::string, nlohmann::json>> nodes;
        listAllNodes("", json, nodes);
        for (auto &i : nodes)
        {
            std::string key = i.first;
            const nlohmann::json &value = i.second;
            if (key.empty())
            {
                continue;
            }
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            ConfigVarBase::ptr var = lookupBase(key);
            if (var)
            {
                var->fromJson(value);
            }
        }
    }

    void Config::loadFromFile(const std::string &file)
    {
        std::ifstream ifs(file);
        if (!ifs)
        {
            throw ConfigException("load config file error: " + file);
        }
        std::stringstream ss;
        ss << ifs.rdbuf();
        loadFromJson(nlohmann::json::parse(ss.str()));
        return;
    }

    static std::map<std::string, int64_t> s_file2modifytime;
    static std::mutex s_mutex;
    void Config::loadFromConfDir(const std::string &path, bool force)
    {
        std::filesystem::path conf_dir(path);
        if (!std::filesystem::exists(conf_dir) || !std::filesystem::is_directory(conf_dir))
        {
            throw ConfigException("load config path: " + path + " not exists or not a directory");
        }
        std::list<std::filesystem::path> files;
        for (auto it = std::filesystem::directory_iterator(conf_dir); it != std::filesystem::directory_iterator(); ++it)
        {
            if (std::filesystem::is_regular_file(it->path()) && it->path().extension() == ".json")
            {
                std::string file_absolute = std::filesystem::absolute(it->path()).string();
                int64_t modify_time = std::filesystem::last_write_time(it->path()).time_since_epoch().count();
                {
                    std::lock_guard lock(s_mutex);
                    if (!force && s_file2modifytime.count(file_absolute) &&
                        s_file2modifytime[file_absolute] == modify_time)
                    {
                        continue;
                    }
                    s_file2modifytime[file_absolute] = modify_time;
                }
                loadFromFile(file_absolute);
            }
        }
    }

}

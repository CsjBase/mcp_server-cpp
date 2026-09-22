#include <string>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <list>

using json = nlohmann::json;

static void listAllNodes(const std::string &prefix, const nlohmann::json &json, std::list<std::pair<std::string, nlohmann::json>> &nodes)
{
    if (prefix.find_first_not_of("abcdefghijklmnopqrstuvwxyz._0123456789") != std::string::npos)
    {
        throw std::logic_error("invalid config name: " + prefix + ": " + json.dump());
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
struct A
{
    int a;
    std::string b;
    std::vector<int> c;
};

void to_json(json &j, const A &a)
{
    j = json{
        {"a", a.a},
        {"b", a.b},
        {"c", a.c}};
}

void from_json(const json &j, A &a)
{
    j.at("a").get_to(a.a);
    j.at("b").get_to(a.b);
    j.at("c").get_to(a.c);
}

int main()
{
    // json j = {{"params", {{"_meta", {{"io.modelcontextprotocol/protocolVersion", 11}}}}}};
    // // json j = {{"io.modelcontextprotocol/protocolVersion", "1.0.0"}};
    // if (j.contains("params") && j["params"].contains("_meta") &&
    //     j["params"]["_meta"].contains("io.modelcontextprotocol/protocolVersion") &&
    //     j["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].is_string())
    // {
    //     std::cout << j["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].get<std::string>() << std::endl;
    //     std::cout << j.dump(4) << std::endl;
    // }
    json j = nullptr;
    if (j.is_null())
    {
        std::cout << "null" << std::endl;
    }
}
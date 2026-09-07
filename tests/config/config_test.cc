#include "config/Config.h"

#include <list>
#include <set>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <iostream>

using namespace config;
ConfigVar<int>::ptr g_int_value_config =
    Config::lookup("system.port", (int)8080, "system port");

ConfigVar<float>::ptr g_float_value_config =
    Config::lookup("system.value", (float)10.2f, "system value");

ConfigVar<std::vector<int>>::ptr g_int_vec_value_config =
    Config::lookup("system.int_vec", std::vector<int>{1, 2}, "system int vec");

ConfigVar<std::list<int>>::ptr g_int_list_value_config =
    Config::lookup("system.int_list", std::list<int>{1, 2}, "system int list");

ConfigVar<std::set<int>>::ptr g_int_set_value_config =
    Config::lookup("system.int_set", std::set<int>{1, 2}, "system int set");

ConfigVar<std::unordered_set<int>>::ptr g_int_uset_value_config =
    Config::lookup("system.int_uset", std::unordered_set<int>{1, 2}, "system int uset");

ConfigVar<std::map<std::string, int>>::ptr g_str_int_map_value_config =
    Config::lookup("system.str_int_map", std::map<std::string, int>{{"k", 2}}, "system str int map");

ConfigVar<std::unordered_map<std::string, int>>::ptr g_str_int_umap_value_config =
    Config::lookup("system.str_int_umap", std::unordered_map<std::string, int>{{"k", 2}}, "system str int map");

void test_config()
{
    std::cout << "before: " << g_int_value_config->getValue() << std::endl;
    std::cout << "before: " << g_float_value_config->getValue() << std::endl;

#define XX(g_var, name, prefix)                                                          \
    {                                                                                    \
        auto &v = g_var->getValue();                                                     \
        for (auto &i : v)                                                                \
        {                                                                                \
            std::cout << #prefix " " #name ": " << i << std::endl;                       \
        }                                                                                \
        std::cout << #prefix " " #name " json: " << g_var->toJson().dump() << std::endl; \
    }

#define XX_M(g_var, name, prefix)                                                        \
    {                                                                                    \
        auto &v = g_var->getValue();                                                     \
        for (auto &i : v)                                                                \
        {                                                                                \
            std::cout << #prefix " " #name ": {"                                         \
                      << i.first << " - " << i.second << "}" << std::endl;               \
        }                                                                                \
        std::cout << #prefix " " #name " json: " << g_var->toJson().dump() << std::endl; \
    }

    XX(g_int_vec_value_config, int_vec, before);
    XX(g_int_list_value_config, int_list, before);
    XX(g_int_set_value_config, int_set, before);
    XX(g_int_uset_value_config, int_uset, before);
    XX_M(g_str_int_map_value_config, str_int_map, before);
    XX_M(g_str_int_umap_value_config, str_int_umap, before);

    Config::loadFromFile("../../../conf/test.json");
    std::cout << "===============================================" << std::endl;

    std::cout << "after: " << g_int_value_config->getValue() << std::endl;
    std::cout << "after: " << g_float_value_config->getValue() << std::endl;

    XX(g_int_vec_value_config, int_vec, after);
    XX(g_int_list_value_config, int_list, after);
    XX(g_int_set_value_config, int_set, after);
    XX(g_int_uset_value_config, int_uset, after);
    XX_M(g_str_int_map_value_config, str_int_map, after);
    XX_M(g_str_int_umap_value_config, str_int_umap, after);
}

struct Person
{
    std::string name;
    int age = 0;
    bool sex = 0;
    bool operator==(const Person &other) const
    {
        return name == other.name &&
               age == other.age &&
               sex == other.sex;
    }
};

using json = nlohmann::json;

void to_json(json &j, const Person &p)
{
    j = json{
        {"name", p.name},
        {"age", p.age},
        {"sex", p.sex}};
}

void from_json(const json &j, Person &p)
{
    j.at("name").get_to(p.name);
    j.at("age").get_to(p.age);
    j.at("sex").get_to(p.sex);
}

ConfigVar<Person>::ptr g_person =
    Config::lookup("class.person", Person(), "system person");

ConfigVar<std::map<std::string, Person>>::ptr g_person_map =
    Config::lookup("class.map_person", std::map<std::string, Person>(), "system person");

ConfigVar<std::map<std::string, std::vector<Person>>>::ptr g_person_vec_map =
    Config::lookup("class.vec_map_person", std::map<std::string, std::vector<Person>>(), "system person");

std::ostream &operator<<(std::ostream &o, const Person &p)
{
    o << "name: " << p.name << " age: " << p.age << " sex:" << p.sex ? "F" : "M";
    return o;
}

void test_class()
{
    std::cout << "class.person before: " << g_person->getValue() << " - " << g_person->toJson().dump() << std::endl;
#define XX_PM(g_var, prefix)                                                          \
    {                                                                                 \
        auto m = g_person_map->getValue();                                            \
        for (auto &i : m)                                                             \
        {                                                                             \
            std::cout << prefix << ": " << i.first << " - " << i.second << std::endl; \
        }                                                                             \
        std::cout << prefix << ": size=" << m.size() << std::endl;                    \
    }

    g_person->addChangeCallback([](const Person &old_value, const Person &new_value)
                                { std::cout << "old_value=" << old_value
                                            << " new_value=" << new_value << std::endl; });

    XX_PM(g_person_map, "class.map_person before");
    std::cout << "class.vec_map_person before: " << g_person_vec_map->toJson().dump() << std::endl;

    Config::loadFromFile("../../../conf/test_class.json");
    std::cout << "===============================================" << std::endl;

    std::cout << "class.person after: " << g_person->getValue() << " - " << g_person->toJson().dump() << std::endl;
    XX_PM(g_person_map, "class.map_person after");
    std::cout << "class.vec_map_person after: " << g_person_vec_map->toJson().dump() << std::endl;
}

int main()
{
    test_config();
    std::cout << "===============================================" << std::endl;
    test_class();
}
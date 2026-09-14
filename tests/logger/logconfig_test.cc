#include "logger/LogConfig.h"

#include <fstream>
#include <iostream>

using json = nlohmann::json;

void showLoggerConfig(const logger::LoggerConfig &l)
{
    std::cout << "name = " << l.name << std::endl;
    for (auto &s : l.sinks)
    {
        std::cout << "type = " << static_cast<int>(s.type) << " " << logger::LogSinkConfig::to_string_view(s.type) << std::endl;
        std::cout << "pattern = " << s.pattern << std::endl;
        std::cout << "file = " << s.file << std::endl;
        std::cout << "max_files = " << s.max_files << std::endl;
        std::cout << "max_size = " << s.max_size << std::endl;
        std::cout << "---------------------------------------" << std::endl;
    }
}

int main()
{
    std::string file = "../../../conf/logs.json";
    std::ifstream ifs(file);
    if (!ifs)
    {
        return -1;
    }
    std::stringstream ss;
    ss << ifs.rdbuf();

    json j = json::parse(ss.str());
    logger::LoggerConfig net = j["logs"]["net"];
    logger::LoggerConfig def = j["logs"]["default"];

    showLoggerConfig(net);
    std::cout << "======================================" << std::endl;
    showLoggerConfig(def);
    std::cout << "======================================" << std::endl;
    json j2 = net;
    std::cout << j2.dump(4) << std::endl;
    std::cout << "======================================" << std::endl;
    json j3 = def;
    std::cout << j3.dump(4) << std::endl;
}
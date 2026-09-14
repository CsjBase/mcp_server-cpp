#include "logger/LoggerManager.h"
#include "logger/LogConfig.h"
#include "logger/log.h"

#include "net/base/Log.h"

#include <iostream>

void test_config_logger()
{
    logger::initLogConfig(); // fix 静态库需要显式初始化
    config::Config::loadFromFile("../conf/logs.json");
    std::cout << logger::LoggerManager::instance().toJsonString() << std::endl;

    LOG_INFO(LOGGER_DEFAULT(), "hello world");
    LOG_INFO(LOGGER_NAME("net"), "hello world");
    NET_LOG_INFO("hello world");
}
int main()
{
    test_config_logger();
}
#include "logger/log.h"

void stdout_logger_example();
void basic_example();
void rotating_example();
void rotating_handler_example();
void rotating_multi_handler_example();
void daily_example();
void daily_handler_example();
void daily_multi_handler_example();
void async_example();
void multi_sink_example();

int main()
{
    try
    {
        LOGGER_DEFAULT()->set_level(logger::LogLevel::Trace);
        LOG_TRACE(LOGGER_DEFAULT(), "Hello, World!");
        LOG_DEBUG(LOGGER_DEFAULT(), "Hello, World!");
        LOG_INFO(LOGGER_DEFAULT(), "Hello, World!");
        LOG_WARN(LOGGER_DEFAULT(), "Hello, World!");
        LOG_ERROR(LOGGER_DEFAULT(), "Hello, World!");
        LOG_FATAL(LOGGER_DEFAULT(), "Hello, World!");
        LOG_INFO(LOGGER_DEFAULT(), "Support for int: {0:d};  hex: {0:x};  oct: {0:o}; bin: {0:b}", 42);
        LOG_INFO(LOGGER_DEFAULT(), "Support for floats {:03.2f}", 1.23456);
        LOG_INFO(LOGGER_DEFAULT(), "Positional args are {1} {0}..", "too", "supported");
        LOG_INFO(LOGGER_DEFAULT(), "{:>8} aligned, {:<8} aligned", "right", "left");

        stdout_logger_example();
        basic_example();
        rotating_example();
        rotating_handler_example();
        rotating_multi_handler_example();
        daily_example();
        daily_handler_example();
        daily_multi_handler_example();
        async_example();
        multi_sink_example();
    }
    catch (const logger::LogException &e)
    {
        std::cerr << e.what() << std::endl;
    }
}

#include "logger/sinks/StdoutLogSink.h"
void stdout_logger_example()
{
    auto stdout_logger = logger::create_stdout_mt_logger("stdout_logger");
    LOG_INFO(stdout_logger, "Hello, World!");

    auto stderr_logger = logger::create_stderr_mt_logger("stderr_logger");
    LOG_INFO(stderr_logger, "Hello, World!");
}

#include "logger/sinks/BasicFileLogSink.h"
void basic_example()
{
    auto basic_file_logger = logger::create_basic_file_mt_logger("basic_file_logger", "logs/basic_file.log");
    LOG_INFO(basic_file_logger, "Hello, World!");
}

#include "logger/sinks/RotatingFileLogSink.h"
void rotating_example()
{
    auto rotating_file_logger = logger::create_rotating_file_mt_logger("rotating_file_logger", "logs/rotating_file_logger.log", 1024 * 1024 * 5, 3);
    for (int i = 0; i < 100000; ++i)
    {
        LOG_INFO(rotating_file_logger, "This is a log line #{}", i);
    }
}

#include "logger/rotater/CompressHandler.h"
void rotating_handler_example()
{
    auto compress_handler = logger::create_compress_handler();
    auto rotating_compress_file_logger = logger::create_rotating_file_mt_logger("rotating_compress_file_logger", "logs/rotating_compress_file_logger.log", 1024 * 1024 * 5, 3, false, std::move(compress_handler));
    for (int i = 0; i < 100000; ++i)
    {
        LOG_INFO(rotating_compress_file_logger, "This is a log line #{}", i);
    }
}

#include "logger/rotater/RotatedFileHandler.h"
#include "logger/rotater/EncryptHandler.h"
void rotating_multi_handler_example()
{
    // 先压缩再加密
    auto compress_handler = logger::create_compress_handler();
    auto encrypt_handler = logger::create_encrypt_handler("aes_key");
    // auto composite_handler = logger::create_composite_handler({std::move(compress_handler), std::move(encrypt_handler)});
    std::unique_ptr<logger::CompositeHandler> composite_handler = std::make_unique<logger::CompositeHandler>();
    composite_handler->add(std::move(compress_handler));
    composite_handler->add(std::move(encrypt_handler));

    auto rotating_compress_and_encrypt_file_logger = logger::create_rotating_file_mt_logger("rotating_compress_and_encrypt_file_logger", "logs/rotating_compress_and_encrypt_file_logger.log", 1024 * 1024 * 5, 3, false, std::move(composite_handler));
    for (int i = 0; i < 100000; ++i)
    {
        LOG_INFO(rotating_compress_and_encrypt_file_logger, "This is a log line #{}", i);
    }
}

#include "logger/sinks/DailyFileLogSink.h"
void daily_example()
{
    // 每天4:30会创建一个新文件。
    auto daily_file_logger = logger::create_daily_file_mt_logger("daily_file_logger", "logs/daily_file.log", 4, 30, false, 7);
    for (int i = 0; i < 100000; ++i)
    {
        LOG_INFO(daily_file_logger, "This is a log line #{}", i);
    }
}
void daily_handler_example()
{
    auto compress_handler = logger::create_compress_handler();
    auto daily_compress_file_logger = logger::create_daily_file_mt_logger("daily_compress_file_logger", "logs/daily_compress_file_logger.log", 4, 30, false, 7, std::move(compress_handler));
    for (int i = 0; i < 100000; ++i)
    {
        LOG_INFO(daily_compress_file_logger, "This is a log line #{}", i);
    }
}
void daily_multi_handler_example()
{
    // 先压缩再加密
    auto compress_handler = logger::create_compress_handler();
    auto encrypt_handler = logger::create_encrypt_handler("aes_key");
    // auto composite_handler = logger::create_composite_handler({std::move(compress_handler), std::move(encrypt_handler)});
    std::unique_ptr<logger::CompositeHandler> composite_handler = std::make_unique<logger::CompositeHandler>();
    composite_handler->add(std::move(compress_handler));
    composite_handler->add(std::move(encrypt_handler));

    auto daily_compress_and_encrypt_file_logger = logger::create_daily_file_mt_logger("daily_compress_and_encrypt_file_logger", "logs/daily_compress_and_encrypt_file_logger.log", 4, 30, false, 7, std::move(composite_handler));
    for (int i = 0; i < 100000; ++i)
    {
        LOG_INFO(daily_compress_and_encrypt_file_logger, "This is a log line #{}", i);
    }
}

#include "logger/AsyncLogger.h"
#include "logger/AsynchronousFactory.h"
void async_example()
{
    auto async_file_logger =
        logger::create_basic_file_mt_logger<logger::AsynchronousFactory>("async_file_logger", "logs/async_file.log");
    LOG_INFO(async_file_logger, "Hello, World!");
}

#include "logger/sinks/ColorLogSink.h"
#include "logger/SynchronousFactory.h"
void multi_sink_example()
{
    auto console_sink = std::make_shared<logger::StderrColorLogSinkMT>();
    console_sink->set_level(logger::LogLevel::Warn);
    console_sink->set_pattern("[multi_sink_example] [%^%l%$] %v");

    auto file_sink = std::make_shared<logger::BasicFileLogSinkMT>("logs/multi_sink.log", true);
    file_sink->set_level(logger::LogLevel::Trace);

    std::vector<std::shared_ptr<logger::LogSink>> sinks({console_sink, file_sink});

    // logger::Logger multi_sink_logger("multi_sink_logger", {console_sink, file_sink});
    // auto multi_sink_logger = logger::create_logger("multi_sink_logger", {console_sink, file_sink});
    auto multi_sink_logger = logger::create_logger("multi_sink_logger", sinks.begin(), sinks.end());
    multi_sink_logger->set_level(logger::LogLevel::Debug);
    multi_sink_logger->warn("this should appear in both console and file");
    multi_sink_logger->info("this message should not appear in the console, only in the file");
}
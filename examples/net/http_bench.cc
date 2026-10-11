/**
 * @file http_bench.cc
 * @brief HTTP 解析器性能基准（纯 CPU 微基准，不涉及网络 I/O）。
 *
 * 测量 HttpRequestParser / HttpResponseParser（Ragel 状态机）单核吞吐。
 * 模拟服务器真实路径（对齐 HttpContext::parseRequest）：
 *   1. reset()：重建 HttpRequest/HttpResponse 对象 + 重绑回调
 *   2. execute()：解析至 header_done（本设计解析器只消费头部，body 由调用方处理）
 *   3. 有 Content-Length 时 setBody() 拷贝 body —— 计入每消息总成本
 * 每轮校验 isFinished/hasError，任何失败计入 errors。
 *
 * 场景:
 *   0  GET 无 body      (~120B 头部)
 *   1  POST 1KB body
 *   2  GET 10 头部      (~600B，header 密集)
 *   3  POST 64KB body   (大 body 拷贝成本)
 *   4  响应解析器        (HTTP/1.1 200 + 1KB body)
 *
 * 用法: http_bench [-n total_requests]   默认每个场景 100000 条
 *
 * 参考: node http-parser bench.c 在裸机现代 CPU 约 40-80 万 req/s/核；
 *       虚拟化环境数字会明显偏低，横向对比需同环境。
 */
#include "net/http/HttpRequestParser.h"
#include "net/http/HttpResponseParser.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <inttypes.h>
#include <string>
#include <vector>

namespace
{
    double nowSec()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    struct Scenario
    {
        const char *name;
        std::string header; ///< 头部区（execute 只消费这部分）
        std::string body;   ///< 消息体（调用方自行拷贝，setBody）
        bool response;      ///< true 用 HttpResponseParser，false 用 HttpRequestParser
    };

    std::vector<Scenario> buildScenarios()
    {
        std::vector<Scenario> v;
        const std::string body1k(1024, 'x');
        const std::string body64k(65536, 'x');

        v.push_back({"GET no body (~120B)",
                     "GET /index.html HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Connection: keep-alive\r\n\r\n",
                     "", false});
        v.push_back({"POST 1KB body",
                     "POST /api/upload HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: 1024\r\n\r\n",
                     body1k, false});
        v.push_back({"GET 10 headers (~600B)",
                     "GET /search?q=benchmark HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36\r\n"
                     "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
                     "Accept-Language: en-US,en;q=0.5\r\n"
                     "Accept-Encoding: gzip, deflate, br\r\n"
                     "Referer: http://www.example.com/index.html\r\n"
                     "Cache-Control: max-age=0\r\n"
                     "Upgrade-Insecure-Requests: 1\r\n"
                     "Cookie: sessionid=abc123def456; theme=dark\r\n"
                     "DNT: 1\r\n\r\n",
                     "", false});
        v.push_back({"POST 64KB body",
                     "POST /upload HTTP/1.1\r\n"
                     "Host: www.example.com\r\n"
                     "Content-Length: 65536\r\n\r\n",
                     body64k, false});
        v.push_back({"response 1KB body",
                     "HTTP/1.1 200 OK\r\n"
                     "Date: Tue, 04 Jun 2019 15:43:56 GMT\r\n"
                     "Server: nginx\r\n"
                     "Content-Type: text/html\r\n"
                     "Content-Length: 1024\r\n"
                     "Connection: keep-alive\r\n\r\n",
                     body1k, true});
        return v;
    }

    void report(const Scenario &s, int64_t iters, double sec, int64_t errors)
    {
        size_t total = s.header.size() + s.body.size();
        double mbs = (static_cast<double>(total) * iters) / (sec * 1024 * 1024);
        printf("%-24s %10.0f req/s  %9.1f MB/s  %8.0f ns/req  %7.2f s  errors %" PRId64 "\n",
               s.name, iters / sec, mbs, sec / iters * 1e9, sec, errors);
    }

    /// 单条消息：reset + 头部解析 + body 拷贝，返回是否有错误
    bool parseOnce(Scenario &s, net::http::HttpRequestParser &parser)
    {
        parser.reset();
        parser.execute(s.header.data(), s.header.size());
        if (!parser.isFinished() || parser.hasError())
            return true;
        if (!s.body.empty())
            parser.getData()->setBody(s.body);
        return false;
    }

    /// 响应解析器 execute 签名要求 char*，且内部会 memmove 未消费的尾部；
    /// 此处只喂头部区（全量消费，memmove 零字节无副作用），body 单独走 setBody
    bool parseOnce(Scenario &s, net::http::HttpResponseParser &parser)
    {
        parser.reset();
        parser.execute(s.header.data(), s.header.size(), true);
        if (!parser.isFinished() || parser.hasError())
            return true;
        if (!s.body.empty())
            parser.getData()->setBody(s.body);
        return false;
    }

    void benchScenario(Scenario &s, int64_t iters, bool warmupOnly)
    {
        int64_t errors = 0;
        double t0 = nowSec();
        if (s.response)
        {
            net::http::HttpResponseParser parser;
            for (int64_t i = 0; i < iters; ++i)
                errors += parseOnce(s, parser);
        }
        else
        {
            net::http::HttpRequestParser parser;
            for (int64_t i = 0; i < iters; ++i)
                errors += parseOnce(s, parser);
        }
        if (!warmupOnly)
            report(s, iters, nowSec() - t0, errors);
    }
} // namespace

int main(int argc, char *argv[])
{
    int64_t iters = 100000;
    if (argc > 1)
    {
        if (std::string(argv[1]) == "-n" && argc > 2)
            iters = atoll(argv[2]);
        else
        {
            printf("Usage: %s [-n total_requests]\n", argv[0]);
            return 1;
        }
    }
    if (iters <= 0)
        return 1;

    auto scenarios = buildScenarios();
    printf("http parser bench: %" PRId64 " messages per scenario (warmup 10%%)\n\n", iters);
    printf("%-24s %12s  %10s  %10s  %8s  %s\n", "scenario", "req/s", "MB/s", "ns/req", "time", "errors");
    printf("%s\n", std::string(92, '-').c_str());

    for (auto &s : scenarios)
    {
        benchScenario(s, std::max<int64_t>(iters / 10, 1000), true); // 预热剔除冷启动
        benchScenario(s, iters, false);
    }
    return 0;
}

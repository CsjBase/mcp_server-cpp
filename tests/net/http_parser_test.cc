#include "net/http/HttpRequestParser.h"
#include "net/http/HttpResponseParser.h"
#include "net/base/Log.h"

const char test_request_data[] = "POST /abc/ HTTP/1.1\r\n"
                                 "Host: www.sylar.top\r\n"
                                 "Content-Length: 10\r\n\r\n"
                                 "1234567890";

void test_request()
{
    net::http::HttpRequestParser parser;
    std::string tmp(test_request_data);
    size_t s = parser.execute(&tmp[0], tmp.size());
    NET_LOG_ERROR("execute rt={} has_error={} is_finished={} total={} content_length={}", s, parser.hasError(), parser.isFinished(), tmp.size(), parser.getContentLength());
    tmp.resize(tmp.size() - s);
    NET_LOG_INFO(parser.getData()->toString());
    NET_LOG_INFO(parser.getData()->getPath());
    NET_LOG_INFO(tmp);
}

const char test_response_data[] = "HTTP/1.1 200 OK\r\n"
                                  "Date: Tue, 04 Jun 2019 15:43:56 GMT\r\n"
                                  "Server: Apache\r\n"
                                  "Last-Modified: Tue, 12 Jan 2010 13:48:00 GMT\r\n"
                                  "ETag: \"51-47cf7e6ee8400\"\r\n"
                                  "Accept-Ranges: bytes\r\n"
                                  "Content-Length: 81\r\n"
                                  "Cache-Control: max-age=86400\r\n"
                                  "Expires: Wed, 05 Jun 2019 15:43:56 GMT\r\n"
                                  "Connection: Close\r\n"
                                  "Content-Type: text/html\r\n\r\n"
                                  "<html>\r\n"
                                  "<meta http-equiv=\"refresh\" content=\"0;url=http://www.baidu.com/\">\r\n"
                                  "</html>\r\n";

void test_response()
{
    net::http::HttpResponseParser parser;
    std::string tmp = test_response_data;
    size_t s = parser.execute(&tmp[0], tmp.size(), true);
    NET_LOG_ERROR("execute rt={} has_error={} is_finished={} total={} content_length={} tmp[s]={}", s, parser.hasError(), parser.isFinished(), tmp.size(), parser.getContentLength(), tmp[s]);

    tmp.resize(tmp.size() - s);

    NET_LOG_INFO(parser.getData()->toString());
    NET_LOG_INFO(tmp);
}

int main(int argc, char **argv)
{
    test_request();
    NET_LOG_INFO("--------------");
    test_response();
    return 0;
}

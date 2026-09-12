#include "net/http/HttpRequest.h"
#include "net/http/HttpResponse.h"

void test_request()
{
    net::http::HttpRequest::ptr req(new net::http::HttpRequest);
    req->setHeader("host", "www.sylar.top");
    req->setBody("hello sylar");
    req->dump(std::cout) << std::endl;
}

void test_response()
{
    net::http::HttpResponse::ptr rsp(new net::http::HttpResponse);
    rsp->setHeader("X-X", "sylar");
    rsp->setBody("hello sylar");
    rsp->setStatus((net::http::HttpStatus)400);
    rsp->setClose(false);

    rsp->dump(std::cout) << std::endl;
}

int main(int argc, char **argv)
{
    test_request();
    test_response();
    return 0;
}

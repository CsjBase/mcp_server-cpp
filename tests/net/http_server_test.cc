#include "net/http/HttpServer.h"
#include "logger/log.h"
#include "net/EventLoop.h"

#define XX(...) #__VA_ARGS__
void run()
{
    net::EventLoop loop;
    net::Address::ptr addr = std::make_shared<net::InetAddress>(8020);
    net::http::HttpServer server(true, &loop, addr, "MyHttpServer", net::TcpServer::Option::kReusePort);

    auto sd = server.getServletDispatch();
    sd->addServlet("/myhttp/xx", [](net::http::HttpRequest::ptr req, net::http::HttpResponse::ptr rsp)
                   {
            rsp->setBody(req->toString());
            return 0; });

    sd->addGlobServlet("/myhttp/*", [](net::http::HttpRequest::ptr req, net::http::HttpResponse::ptr rsp)
                       {
            rsp->setBody("Glob:\r\n" + req->toString());
            return 0; });

    sd->addGlobServlet("/myhttpx/*", [](net::http::HttpRequest::ptr req, net::http::HttpResponse::ptr rsp)
                       {
            rsp->setBody(XX(<html>
<head><title>404 Not Found</title></head>
<body>
<center><h1>404 Not Found</h1></center>
<hr><center>nginx/1.16.0</center>
</body>
</html>
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
<!-- a padding to disable MSIE and Chrome friendly error page -->
));
            return 0; });

    server.start();
    loop.loop();
}

int main(int argc, char **argv)
{
    run();
    return 0;
}

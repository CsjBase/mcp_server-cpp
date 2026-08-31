#include "net/TcpServer.h"
#include "logger/log.h"

using namespace net;

class EchoServer
{
public:
    EchoServer(EventLoop *loop, Address::ptr listenAddr, const std::string &name)
        : m_server(loop, listenAddr, name)
    {
        m_server.setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        m_server.setMessageCallback(std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    }

    void onConnection(const TcpConnection::ptr &conn)
    {
        LOG_INFO(LOGGER_DEFAULT(), "{} is {}", conn->peerAddress()->to_string(), conn->connected() ? "UP" : "DOWN");
    }

    void onMessage(const TcpConnection::ptr &conn, Buffer *buffer, Timestamp receiveTime)
    {
        std::string msg = buffer->retrieveAllAsString();
        std::cout << receiveTime.toString() << " " << conn->peerAddress()->to_string() << ":" << msg << std::endl;
        conn->send(msg);
    }

    void start()
    {
        m_server.start();
    }

private:
    TcpServer m_server;
};

int main()
{
    EventLoop loop;
    Address::ptr addr = std::make_shared<InetAddress>(8081);
    EchoServer server(&loop, addr, "EchoServer");
    server.start();
    loop.loop();
}
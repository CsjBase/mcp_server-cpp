#include "net/EventLoop.h"
#include "net/Acceptor.h"
#include "net/TcpConnection.h"
#include "logger/log.h"

#include <unordered_map>

using namespace net;

class EchoServer
{
public:
    EchoServer()
        : m_local(std::make_shared<InetAddress>(8021)), m_loop(), m_accept(&m_loop, m_local, true)
    {
        m_accept.listen();
        m_accept.setNewConnectionCallback(std::bind(&EchoServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
    }

    void start()
    {
        LOG_INFO(LOGGER_DEFAULT(), " EchoServer start!");
        m_loop.loop();
    }

    void newConnection(int sockfd, Address::ptr peerAddr)
    {
        LOG_INFO(LOGGER_DEFAULT(), "new connection: {}", peerAddr->to_string());
        TcpConnection::ptr conn(std::make_shared<TcpConnection>(&m_loop, peerAddr->to_string(), sockfd, m_local, peerAddr));
        m_conn[peerAddr->to_string()] = conn;
        conn->setConnectionCallback(std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        conn->setMessageCallback(std::bind(&EchoServer::handleRead, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
        conn->setCloseCallback(std::bind(&EchoServer::handleClose, this, std::placeholders::_1));
        conn->connectEstablished(); // unf
    }

    void onConnection(const std::shared_ptr<TcpConnection> &conn)
    {
        LOG_INFO(LOGGER_DEFAULT(), "{} is {}", conn->peerAddress()->to_string(), conn->connected() ? "UP" : "DOWN");
    }

    void handleRead(const std::shared_ptr<TcpConnection> &conn, Buffer *buf, Timestamp recvTime)
    {
        std::string msg = buf->retrieveAllAsString();
        std::cout << recvTime.toString() << " " << conn->peerAddress()->to_string() << ":" << msg << std::endl;
        conn->send(msg);
    }

    void handleClose(const TcpConnection::ptr &conn)
    {
        LOG_INFO(LOGGER_DEFAULT(), "{} close.", conn->peerAddress()->to_string());
        conn->connectDestroyed();
        m_conn.erase(conn->name());
    }

private:
    Address::ptr m_local;
    EventLoop m_loop;
    Acceptor m_accept;

    std::unordered_map<std::string, TcpConnection::ptr> m_conn;
};

int main()
{
    EchoServer server;
    server.start();
}
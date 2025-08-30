#include "chatserver.hpp"
#include "json.hpp"
#include "chatservice.hpp"
#include <functional>
#include <string>
using namespace std;
using namespace placeholders;
using json = nlohmann::json;

// 初始化聊天服务器对象
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册链接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 上报链接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &con)
{
    // 客户端断开连接
    if (!con->connected())
    {
        ChatService::instance()->clientCloseException(con);
        con->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &con, // 当前的连接
                           Buffer *buffer,              // 接收到的数据缓冲区
                           Timestamp time)              // 接收到数据的时间信息
{
    string buf = buffer->retrieveAllAsString();
    
    // 数据的反序列化, 解析出的数据中包含一个事件的id号(在public.hpp中定义的事件id),标识这个事件
    json js = json::parse(buf);
    
    // 目的: 完全解耦网络模块的代码和业务模块的代码
    // 通过js["msgId"]获取对应的业务处理器handler => con、js、time
    auto msgHandler = ChatService::instance()->getHandler(js["msgId"].get<int>());
    // 回调消息绑定好的事件处理器,来进行相应的业务处理
    msgHandler(con, js, time);
}
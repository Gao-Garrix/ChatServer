#include "chatserver.hpp"
#include "chatservice.hpp"
#include <iostream>
#include <signal.h>
using namespace std;

// 处理服务器被ctrl+C强制中断后,重置用户状态信息
void resetHandler(int)
{
    ChatService::instance()->reset();
    exit(0);
}

int main(int argc, char **argv)
{
    if (argc < 3) // 命令中必须提供两个参数: IP地址、端口号
    {
        cerr << "command invalid! example: ./ChatServer 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    signal(SIGINT, resetHandler);

    EventLoop loop;
    InetAddress addr(ip, port);
    ChatServer server(&loop, addr, "ChatServer");

    server.start(); // 启动服务, 将listenfd epoll_ctl添加到epoll中
    loop.loop();    // epoll_wait以阻塞方式等待新用户连接、已连接用户的读写事件等

    return 0;
}
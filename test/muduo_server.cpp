/*
muduo网络库给用户提供了两个主要的类
TcpServer: 用于编写服务器程序
TcpClient: 用于编写客户端程序

好处:能够把 网络I/O 的代码和 业务代码 区分开
                            ↓
                        用户的连接和断开、用户的可读写事件
*/
#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <iostream>
#include <string>
#include <functional> // 引用绑定器
using namespace std;
using namespace muduo;
using namespace muduo::net;
using namespace placeholders;

/*
基于muduo网络库开发服务器程序 技术栈: epoll + 线程池
1. 组合TcpServer对象
2. 创建EventLoop事件循环对象的指针
3. 明确TcpServer构造函数需要什么参数, 输出ChatServer的构造函数
4. 在当前服务器类的构造函数当中，注册处理连接的回调函数和处理读写事件的回调函数
5. 设置合适的服务端线程数量, muduo库会自己分配I/O线程和worker线程

我们最主要的精力放在 onConnection 和 onMessage 函数上,
这是比较模板化的代码,后续用到的时候可以直接复制使用。
*/

class ChatServer
{
private:
    TcpServer _server; // #1 在下面给定构造函数
    EventLoop *_loop;  // #2 事件循环 内部操作epoll

    // 从epoll取出一个假如是listenfd的事件, 这意味着有新的连接请求, 使用accept接受连接, 创建新socket与其通信
    // onConnection函数专门处理用户的连接创建和断开
    /* onConnnection函数是成员方法,写成成员方法是因为想要访问上面的成员变量,
       但是写成成员方法还会产生一个 this 指针,这样onConnection就产生两个参数了,
       导致跟setConnectionCallback只有一个参数不一致,
       所以我们用绑定器绑定this指针对象到onConnection当中就可以了, 绑定器在<functional>头文件中。*/
    void onConnection(const TcpConnectionPtr &con)
    {
        if (con->connected()) // 客户端连接成功,返回true
        {
            // con->peerAddress().toIpPort(): 客户端的ip和端口; con->localAddress().toIpPort(): 服务端的ip和端口
            cout << con->peerAddress().toIpPort() << " -> " << con->localAddress().toIpPort() << " state:online" << endl;
        }
        else 
        {
            cout << con->peerAddress().toIpPort() << " -> " << con->localAddress().toIpPort() << " state:offline" << endl;
            con->shutdown(); // close(fd) 将socket释放掉
            // _loop->quit(); // 退出epoll
        }
    }

    // onMessage函数专门处理用户的读写事件
    // onMessage同样也要绑定this指针对象到onMessage当中
    void onMessage(const TcpConnectionPtr &con, // 当前的连接
                   Buffer *buffer,              // 接收到的数据缓冲区
                   Timestamp time)              // 接收到数据的时间信息
    {
        // retrieveAllAsString函数: 将缓冲区接收到的数据全部转成字符串, 并赋值给string buf。同时，这些数据会从 buffer 缓冲区对象中移除。
        string buf = buffer->retrieveAllAsString();

        // time.toString(): 将time对象存储的时间信息转为字符串
        cout << "receive data: " << buf << "time: " << time.toString() << endl;
        
        // 服务端接收到用户的数据,经过解码和处理数据后, 再返回这些数据给客户端（收到啥就返回啥）
        con->send(buf);
    }

public:
    ChatServer(EventLoop *loop,               // 事件循环
               const InetAddress &listenAddr, // 绑定的ip地址和端口号
               const string &nameArg)         // 给服务器命名
        : _server(loop, listenAddr, nameArg), _loop(loop)
    {
        /* std::bind 创建一个新的可调用对象, 这个对象封装了对onConnection函数的调用。
           this 指针指向_server对象, this指针被传递给 std::bind, 这样封装的可调用对象就知道它应该在哪个 ChatServer 对象实例上调用 onConnection 函数。
           参数占位符 _1 表示 onConnection 函数的第一个参数将在实际调用时提供。
           setConnectionCallback 函数被调用，并将 std::bind 创建的可调用对象作为参数传递。
           这样，_server 对象就注册了一个回调函数，当epoll检测到有新的连接创建和断开时，这个回调函数会被调用。
        */
        // 给服务器注册用户的连接创建和断开回调
        _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

        /* std::bind 创建一个新的可调用对象，这个对象封装了对onMessage函数的调用。
           this 指针指向_server对象, this指针被传递给 std::bind，这样封装的可调用对象就知道它应该在哪个 ChatServer 对象实例上调用 onMessage 函数。
           参数占位符_1, _2, _3 表示 onMessage 函数的前三个参数将在实际调用时提供。
           setMessageCallback 函数被调用，并将 std::bind 创建的可调用对象作为参数传递。
           这样，_server 对象就注册了一个回调函数，当有新的读写事件发生时，这个回调函数会被调用。
        */
        // 给服务器注册用户读写事件回调
        _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

        /* muduo库是epoll+多线程，muduo库会自适应，现在只有一个线程，这个线程里有一个epoll，
           它不仅要监听新用户的连接，还要处理已连接用户的读写事件，效率较低。所以我们还需设置服务器端
           的线程数量。
           若设置为2, 则一个I/O线程的epoll专门监听新用户连接，另一个工作线程的epoll专门处理连接的断开和已连接用户的读写事件
        */
        // 设置服务器端的线程数量为4: 1个I/O线程 3个工作线程
        _server.setThreadNum(4);
    }

    // 启动ChatServer服务 开启事件循环
    void start()
    {
        _server.start();
    }

};

int main()
{
    EventLoop loop; // epoll
    InetAddress addr("127.0.0.1", 6000);
    ChatServer server(&loop, addr, "ChatServer");
    server.start(); // 启动服务, 将listenfd epoll_ctl添加到epoll中
    loop.loop(); // epoll_wait以阻塞方式等待新用户连接、已连接用户的读写事件等
    return 0;
}
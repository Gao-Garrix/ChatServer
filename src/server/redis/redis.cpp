#include "redis.hpp"
#include <iostream>
using namespace std;

// 构造函数 将两个上下文指针初始化为空指针
Redis::Redis()
    : _publish_context(nullptr), _subcribe_context(nullptr)
{
}

Redis::~Redis()
{
    if (_publish_context != nullptr)
    {
        redisFree(_publish_context); // 释放发布消息上下文的资源
    }

    if (_subcribe_context != nullptr)
    {
        redisFree(_subcribe_context); // 释放订阅消息上下文的资源
    }
}

bool Redis::connect()
{
    // 负责publish发布消息的上下文连接
    _publish_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _publish_context)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    _subcribe_context = redisConnect("127.0.0.1", 6379);
    if (nullptr == _subcribe_context)
    {
        cerr << "connect redis failed!" << endl;
        return false;
    }

    // 在单独的线程中, 监听订阅的通道上的事件, 有消息就给业务层进行上报
    thread t([&]() {
        observer_channel_message();
    });
    t.detach(); // 设置分离线程, 线程运行完资源自动回收

    cout << "connect redis-server success!" << endl;

    return true;
}

// 向redis指定的通道channel发布消息
bool Redis::publish(int channel, string message)
{
    // redisCommand 函数返回值是 redisReply结构体
    redisReply *reply = (redisReply *)redisCommand(_publish_context, "PUBLISH %d %s", channel, message.c_str()); // 输入publish命令, 发布消息
    if (nullptr == reply) // 发布消息失败
    {
        cerr << "publish command failed!" << endl;
        return false;
    }
    freeReplyObject(reply); // 发布成功, 释放redisReply对象占用的内存
    return true;
}

// 向redis指定的通道subscribe订阅消息
bool Redis::subscribe(int channel)
{
    // SUBSCRIBE命令本身会造成线程阻塞等待通道里面发生消息，这里只做订阅通道，不接收通道消息
    // 通道消息的接收专门在observer_channel_message函数中的独立线程中进行
    // 只负责发送命令，不阻塞接收redis server响应消息，否则和notifyMsg线程抢占响应资源

    // redisAppendCommand 函数作用是将命令写到本地缓存
    if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "SUBSCRIBE %d", channel))
    {
        cerr << "subscribe command failed!" << endl;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done)
    {
        // redisBufferWrite 函数作用是从本地缓存把命令发送到redis server
        if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
        {
            cerr << "subscribe command failed!" << endl;
            return false;
        }
    }
    // redisGetReply 以阻塞方式等待redis服务器响应

    return true;
}

// 向redis指定的通道unsubscribe取消订阅消息
bool Redis::unsubscribe(int channel)
{
    // redisAppendCommand函数作用是将命令写到本地缓存
    if (REDIS_ERR == redisAppendCommand(this->_subcribe_context, "UNSUBSCRIBE %d", channel))
    {
        cerr << "unsubscribe command failed!" << endl;
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done)
    {
        if (REDIS_ERR == redisBufferWrite(this->_subcribe_context, &done))
        {
            cerr << "unsubscribe command failed!" << endl;
            return false;
        }
    }
    return true;
}

// 在独立线程中接收订阅通道中的消息
void Redis::observer_channel_message()
{
    redisReply *reply = nullptr;

    // redisGetReply从订阅消息上下文以阻塞方式等待redis服务器响应
    while (REDIS_OK == redisGetReply(this->_subcribe_context, (void **)&reply))
    {
        // 订阅收到的消息是一个带三元素的数组 element[2]是消息内容 element[1]是通道号
        // reply 不为空（表示成功获取到响应）。
        // reply->element[2] 不为空（表示消息内容存在）。
        // reply->element[2]->str 不为空（表示消息内容的字符串有效）
        if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        {
            // 给业务层上报通道上发生的消息
            // atoi(reply->element[1]->str) 将通道号字符串转换为整型
            // reply->element[2]->str 是消息内容的字符串
            _notify_message_handler(atoi(reply->element[1]->str) , reply->element[2]->str);
        }

        freeReplyObject(reply); // 释放redisReply对象占用的内存
    }

    cerr << ">>>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<<" << endl;
}

void Redis::init_notify_handler(function<void(int,string)> fn)
{
    this->_notify_message_handler = fn; // 注册回调
}
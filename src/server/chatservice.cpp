#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h> // 引用muduo库的日志
#include <vector>
using namespace std;
using namespace muduo;

// 获取单例对象的接口函数
ChatService *ChatService::instance()
{
    // 定义单例对象 线程安全
    static ChatService service;
    return &service;
}

// 注册消息以及对应的Handler回调操作
ChatService::ChatService()
{
    // 用户基本业务管理相关事件处理回调注册
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({LOGINOUT_MSG, std::bind(&ChatService::loginout, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, std::bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, std::bind(&ChatService::addFriend, this, _1, _2, _3)});

    // 群组业务管理相关事件处理回调注册
    _msgHandlerMap.insert({CREATE_GROUP_MSG, std::bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, std::bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, std::bind(&ChatService::groupChat, this, _1, _2, _3)});

    // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调函数
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}

// 服务器异常中断,业务重置方法
void ChatService::reset()
{
    // 把处于online状态的用户重置为offline
    _userModel.resetState();
}

// 获取某消息id对应的Handler处理器
MsgHandler ChatService::getHandler(int msgId)
{
    // 记录错误日志, msgId没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgId);
    if (it == _msgHandlerMap.end())
    {
        // 使用lambda表达式 [=]值捕获 返回一个默认的处理器, 空操作
        return [=](const TcpConnectionPtr &con, json &js, Timestamp)
        {
            LOG_ERROR << "msgId:" << msgId << " can not find handler!";
        };
    }
    else
    {
        // 找到了该消息id的Handler处理器
        return _msgHandlerMap[msgId];
    }
}

// 处理登录业务  id password
void ChatService::login(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int id = js["id"].get<int>(); // 获取接收到客户端发来的用户id数据
    string pwd = js["password"];  // 获取接收到客户端发来的登录密码数据

    User user = _userModel.query(id);               // 传入用户id,查询并返回该id对应的数据
    if (user.getId() == id && user.getPwd() == pwd) // 该用户存在,且密码输入正确
    {
        if (user.getState() == "online") // 检测到该用户已经登录,应不允许重复登录
        {
            json response;                                                // 创建json对象,存储将要发送的数据
            response["msgId"] = LOGIN_MSG_ACK;                            // 设置事件id为登录响应消息
            response["errno"] = 2;                                        // 设错误号为2
            response["errmsg"] = "this account is using, input another!"; // 给出错误提示信息
            con->send(response.dump());                                   // response.dump()将JSON对象转换为字符串格式,当前连接对象con调用send函数将这些数据发送回给客户端
        }
        else // 登录成功
        {
            // 添加{}表示一个作用域,在{}内加锁,保证线程互斥,出了{}后解锁
            {
                lock_guard<mutex> lock(_connMutex); // lock_guard类的构造函数是加锁,析构函数是解锁,利用智能指针实现自动释放锁
                _userConnMap.insert({id, con});     // 记录用户连接信息
            }

            // 用户id登录成功后, 向redis消息队列订阅通道channel(id)
            _redis.subscribe(id);

            user.setState("online");      // 更新该用户状态信息state: offline => online
            _userModel.updateState(user); // 调用UserModel类的成员方法update,更新数据库user表中该用户的状态信息

            json response;                     // 创建json对象,存储将要发送的数据
            response["msgId"] = LOGIN_MSG_ACK; // 设置事件id为登录响应消息
            response["errno"] = 0;             // 错误号为0则表示响应成功
            response["id"] = user.getId();     // 获取用户id
            response["name"] = user.getName(); // 获取用户名

            // 查询该用户是否有离线消息
            vector<string> msgVec = _offlineMsgModel.query(id);
            if (!msgVec.empty())
            {
                response["offlinemsg"] = msgVec; // 获取离线消息

                // 读取该用户的离线消息后,删除该用户的所有离线消息
                _offlineMsgModel.remove(id);
            }

            // 查询该用户的好友消息
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty())
            {
                vector<string> vec;

                // 将每个好友信息都转成json字符串,存到vec中
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec.push_back(js.dump());
                }
                response["friends"] = vec; // 获取所有好友信息
            }

            // 查询用户的群组信息
            vector<Group> groupuserVec = _groupModel.queryGroups(id);
            if (!groupuserVec.empty())
            {
                // group:[{groupid:[xxx, xxx, xxx, xxx]}]
                vector<string> groupV;
                for (Group &group : groupuserVec) // 群组列表里的所有群组
                {
                    json grpjson;
                    grpjson["id"] = group.getId();
                    grpjson["groupname"] = group.getName();
                    grpjson["groupdesc"] = group.getDesc();

                    vector<string> userV;
                    for (GroupUser &user : group.getUsers()) // 群组里的所有成员
                    {
                        json js;
                        js["id"] = user.getId();
                        js["name"] = user.getName();
                        js["state"] = user.getState();
                        js["role"] = user.getRole();
                        userV.push_back(js.dump());
                    }
                    grpjson["users"] = userV;
                    groupV.push_back(grpjson.dump());
                }
                response["groups"] = groupV;
            }

            con->send(response.dump()); // response.dump()将JSON对象转换为字符串格式,当前连接对象con调用send函数将这些数据发送回给客户端
        }
    }
    else if (user.getId() == id && user.getPwd() != pwd) // 该用户存在,但密码输入错误,登录失败
    {
        json response;                          // 创建json对象,存储将要发送的数据
        response["msgId"] = LOGIN_MSG_ACK;      // 设置事件id为登录响应消息
        response["errno"] = 3;                  // 设错误号为 3
        response["errmsg"] = "Wrong Password!"; // 给出错误提示信息
        con->send(response.dump());             // response.dump()将JSON对象转换为字符串格式,当前连接对象con调用send函数将这些数据发送回给客户端
    }
    else // 该用户不存在(对应 user.getId()== -1 的情况), 登录失败
    {
        json response;                                   // 创建json对象,存储将要发送的数据
        response["msgId"] = LOGIN_MSG_ACK;               // 设置事件id为登录响应消息
        response["errno"] = 1;                           // 设错误号为 1
        response["errmsg"] = "this account is invalid!"; // 给出错误提示信息
        con->send(response.dump());                      // response.dump()将JSON对象转换为字符串格式,当前连接对象con调用send函数将这些数据发送回给客户端
    }
}

// 处理注册业务 name password
void ChatService::reg(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    string name = js["name"];    // 获取客户端发来的名字
    string pwd = js["password"]; // 获取客户端发来的密码

    User user;                            // 创建新用户对象user
    user.setName(name);                   // 设置新用户user的名字
    user.setPwd(pwd);                     // 设置新用户user的密码
    bool state = _userModel.insert(user); // 向User表中添加新用户user
    if (state)                            // 注册成功
    {
        json response;                   // 创建json对象,存储将要发送的数据
        response["msgId"] = REG_MSG_ACK; // 设置事件id为注册响应消息
        response["errno"] = 0;           // 错误号为0则表示响应成功
        response["id"] = user.getId();   // 获取用户id
        con->send(response.dump());      // response.dump()将JSON对象转换为字符串格式,当前连接对象con调用send函数将这些数据发送回给客户端
    }
    else // 注册失败
    {
        json response;                   // 创建json对象,存储将要发送的数据
        response["msgId"] = REG_MSG_ACK; // 设置事件id为注册响应消息
        response["errno"] = 1;           // 错误号为1则表示响应失败,后面不需要再获取用户id了
        con->send(response.dump());      // response.dump()将JSON对象转换为字符串格式,当前连接对象con调用send函数将这些数据发送回给客户端
    }
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    // 线程安全的作用域{}
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid); // 在用户连接表中查找是否有当前用户userid的连接
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it); // 从_userConnMap中删除当前用户userid的连接信息
        }
    }

    // 用户注销, 相当于就是下线, 在redis中取消订阅通道
    _redis.unsubscribe(userid); 

    // 更新用户的状态信息
    User user(userid, "", "", "offline"); // 设置当前用户userid的状态为"offline"
    _userModel.updateState(user);         // 更新数据库中当前用户userid的状态信息
}

// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &con)
{
    User user;

    // 线程安全的作用域{}
    {
        lock_guard<mutex> lock(_connMutex); // 加互斥锁
        for (auto it = _userConnMap.begin(); it != _userConnMap.end(); it++)
        {
            if (it->second == con) // 找到了这个异常退出的连接
            {
                user.setId(it->first); // 获取这个异常退出用户的id

                // 从_userConnMap中删除用户的连接信息
                _userConnMap.erase(it);
                break;
            }
        }
    }

    // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(user.getId());

    // 该用户存在,则更新用户的状态信息
    if (user.getId() != -1)
    {
        user.setState("offline");     // 设置该用户的状态为"offline"
        _userModel.updateState(user); // 更新数据库中该用户的状态信息
    }
}

// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int toid = js["toid"].get<int>(); // 获取消息接收者的id

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid); // 在用户连接表中找toid用户的连接
        if (it != _userConnMap.end()) // 在本服务器中找到了该toid用户连接
        {
            // 该toid用户在线, 服务器主动推送消息给接收者
            it->second->send(js.dump());
            return;
        }
    }

    // 在本服务器中没找到用户toid的连接, 则在数据库查询用户toid是否在线
    User user = _userModel.query(toid);
    if (user.getState() == "online") // 用户toid在线, 表示用户toid在其它服务器上登录了
    {
        _redis.publish(toid, js.dump()); // 将该消息发送到redis的toid通道
        return;
    }

    // 用户toid不在线,存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}

/*
todo: 添加好友、创建群组、加入群组业务对应的成功或失败的响应功能待开发
思路: 在public.hpp中定义这三种响应消息类型(加个后缀 _ACK),然后参考上面用户登录业务的响应消息模块代码进行开发即可
*/

// 添加好友业务 msgId id friendid
void ChatService::addFriend(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();         // 当前用户id
    int friendid = js["friendid"].get<int>(); // 待添加的好友id

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}

// 创建群组业务 id groupname groupdesc
void ChatService::createGroup(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int userid = js["id"].get<int>(); // 正在创建群组的用户id
    string name = js["groupname"];    // 群名
    string desc = js["groupdesc"];    // 群描述

    // 存储新创建的群组信息
    Group group(-1, name, desc);
    if (_groupModel.createGroup(group)) // 群组创建成功,群组id已自动生成
    {
        // 存储群组创建人信息  将群组创建人用户加入群组,获取群组id,设其角色为creator
        _groupModel.addGroup(userid, group.getId(), "creator");
    }
}

// 加入群组业务 id groupid
void ChatService::addGroup(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();                // 要加入群组的用户的id
    int groupid = js["groupid"].get<int>();          // 用户要加入的群组的id
    _groupModel.addGroup(userid, groupid, "normal"); // 将该用户加入群组,设其角色为normal
}

// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();                                     // 发送群聊消息的用户id
    int groupid = js["groupid"].get<int>();                               // 用户发送群聊消息所在的群组id
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid); // 查询该用户userid所在群组groupid的其他用户的id

    lock_guard<mutex> lock(_connMutex); // 加互斥锁,保证操作_userConnMap的线程安全
    for (int id : useridVec)            // 向群组groupid中的其他用户转发用户userid发送的群聊消息
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end()) // 在本服务器找到了该用户id的连接,即该用户在线
        {
            // 转发群消息
            it->second->send(js.dump());
        }
        else
        {
            // 在本服务器中没找到用户id的连接, 则在数据库查询用户id是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online") // 用户id在线, 表示用户id在其它服务器上登录了
            {
                _redis.publish(id, js.dump()); // 将该消息发送到redis的id通道
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }
}

// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end()) // 找到用户userid的连接
    {
        it->second->send(msg); // 发送消息给用户userid
        return;
    }

    // 若用户userid下线了, 存储离线消息
    _offlineMsgModel.insert(userid, msg);
}
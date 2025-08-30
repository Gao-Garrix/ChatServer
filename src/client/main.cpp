/*
头文件的解释
<thread>: 支持多线程编程, 可以创建新的线程、等待线程结束、在线程之间传递数据等;
<chrono>: 提供了时间测量和日期时间操作的功能,它允许你以一种类型安全和可移植的方式处理时间和持续时间。例如，你可以使用std::chrono来测量代码执行的时间，或者设置延时;
<ctime> : 提供了处理日期和时间的标准C函数,可用于获取当前时间、将时间转换为本地时间、格式化时间等;
<unistd.h>: 提供了对UNIX标准定义的符号常量和类型以及对POSIX操作系统API的访问;
<sys/socket.h>: 提供了创建socket、绑定socket、监听连接、接受连接、发送和接收数据等功能;
<netinet/in.h>: 提供了与网络字节顺序和Internet地址格式相关的宏和类型定义,通常与<sys/socket.h>一起使用，用于网络通信;
<sys/types.h>: 定义了各种基本的数据类型,如size_t、ssize_t、off_t等;\
<arpa/inet.h>: 用于网络地址转换,如 inet_addr（IPv4地址点分十进制字符串 -> 二进制形式）和 inet_ntoa（IPv4地址二进制形式 -> 点分十进制字符串）;
<semaphore.h>: 定义了POSIX信号量，用于进程间的同步。信号量控制对共享资源的访问,确保同一时间只有一个进程可以访问资源;
<atomic>: 用于在多线程环境中无锁地操作数据。它确保了操作的原子性，防止了数据竞争。
*/

#include <iostream>
#include <thread>
#include <chrono>
#include <ctime>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include "json.hpp"

using namespace std;
using json = nlohmann::json;

#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "group.hpp"
#include "user.hpp"
#include "public.hpp"

// 记录当前系统登录的用户信息
User g_currentUser;
// 记录当前登录用户的好友列表信息
vector<User> g_currentUserFriendList;
// 记录当前登录用户的群组列表信息
vector<Group> g_currentUserGroupList;
// 控制主菜单页面程序
bool isMainMenuRunning = false;


// 接收线程
void readTaskHandler(int clientfd);
// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime();
// 主聊天页面程序
void mainMenu(int);
// 显示当前登录成功用户的基本信息
void showCurrentUserData();

// 聊天客户端程序实现，main线程用作发送线程，子线程用作接收线程
int main(int argc, char **argv)
{
    if (argc < 3) // 判断命令行输入的命令数量
    {
        cerr << "command invalid! example: ./ChatClient 127.0.0.1 6000" << endl;
        exit(-1);
    }

    // 解析通过命令行参数传递的ip和port
    char *ip = argv[1];
    uint16_t port = atoi(argv[2]);

    // 创建client端的socket
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (-1 == clientfd)
    {
        cerr << "socket create error" << endl;
        exit(-1);
    }

    // 填写client需要连接的server信息ip+port
    sockaddr_in server;
    memset(&server, 0, sizeof(sockaddr_in));

    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(ip);

    // client和server进行连接
    if (-1 == connect(clientfd, (sockaddr *)&server, sizeof(sockaddr_in)))
    {
        cerr << "connect server error" << endl;
        close(clientfd); // 释放资源
        exit(-1);
    }

    // main线程用于接收用户输入，负责发送数据
    for (;;)
    {
        // 显示首页面菜单 登录、注册、退出
        cout << "========================" << endl;
        cout << "1. login" << endl;
        cout << "2. register" << endl;
        cout << "3. quit" << endl;
        cout << "========================" << endl;
        cout << "choice:";
        int choice = 0;
        cin >> choice;
        cin.get(); // 从缓冲区读数据时,也要把读掉缓冲区残留的回车

        switch (choice)
        {
        case 1: // login业务
        {
            int id = 0;
            char pwd[50] = {0};
            cout << "userid:";
            cin >> id;
            cin.get(); // 读掉缓冲区残留的回车
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["msgId"] = LOGIN_MSG;
            js["id"] = id;
            js["password"] = pwd;
            string request = js.dump(); // 将输入的id和password信息序列化成json字符串

            // 通过 clientfd 套接字发送字符串request
            // 若request字符串发送成功,send函数返回发送的字节数给len,否则返回-1
            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1) // 登录消息发送失败
            {
                cerr << "send login msg error:" << request << endl;
            }
            else // 登录消息发送成功
            {
                char buffer[1024] = {0}; // 存储通过套接字接收到的json字符串数据

                // 从 clientfd 套接字接收数据(最多1024字节), 并存储到buffer中
                // 若接收成功,recv函数返回接收的字节数给len,否则返回-1
                len = recv(clientfd, buffer, 1024, 0);
                if (len == -1)
                {
                    cerr << "recv login response error" << endl;
                }
                else
                {
                    json responsejs = json::parse(buffer);   // 反序列化buffer中的字符数据,得到json对象
                    if (responsejs["errno"].get<int>() != 0) // 错误号不为0, 登录失败
                    {
                        cerr << responsejs["errmsg"] << endl;
                    }
                    else // 登录成功
                    {
                        // 记录当前用户的id和name
                        g_currentUser.setId(responsejs["id"].get<int>());
                        g_currentUser.setName(responsejs["name"]);

                        // 记录当前用户的好友列表信息
                        if (responsejs.contains("friends")) // 当前用户有好友
                        {
                            // 初始化好友列表
                            g_currentUserFriendList.clear();

                            vector<string> vec = responsejs["friends"];
                            for (string &str : vec)
                            {
                                json js = json::parse(str); // 反序列化好友信息的字符串
                                User user;
                                user.setId(js["id"].get<int>());
                                user.setName(js["name"]);
                                user.setState(js["state"]);
                                g_currentUserFriendList.push_back(user);
                            }
                        }

                        // 记录当前用户的群组列表信息
                        if (responsejs.contains("groups")) // 当前用户有群组
                        {
                            // 初始化群组列表
                            g_currentUserGroupList.clear();

                            vector<string> vec1 = responsejs["groups"];
                            for (string &groupstr : vec1)
                            {
                                json grpjs = json::parse(groupstr); // 反序列化群组信息的字符串
                                Group group;
                                group.setId(grpjs["id"].get<int>());
                                group.setName(grpjs["groupname"]);
                                group.setDesc(grpjs["groupdesc"]);

                                vector<string> vec2 = grpjs["users"];
                                for (string &userstr : vec2)
                                {
                                    GroupUser user;
                                    json js = json::parse(userstr); // 反序列化群组里的成员信息的字符串
                                    user.setId(js["id"].get<int>());
                                    user.setName(js["name"]);
                                    user.setState(js["state"]);
                                    user.setRole(js["role"]);
                                    group.getUsers().push_back(user);
                                }

                                g_currentUserGroupList.push_back(group);
                            }
                        }

                        // 显示登录用户的基本信息
                        showCurrentUserData();

                        // 显示当前用户的离线消息 个人聊天信息或者群组消息
                        if (responsejs.contains("offlinemsg")) // 当前用户有离线消息
                        {
                            vector<string> vec = responsejs["offlinemsg"];
                            for (string &str : vec)
                            {
                                json js = json::parse(str);
                                // time + [id] + name + "said: " + xxx
                                int msgtype = js["msgId"].get<int>(); // 获取消息类型(单聊或群聊)
                                if (ONE_CHAT_MSG == msgtype)   // 一对一聊天消息
                                {
                                    cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                                         << " said: " << js["msg"].get<string>() << endl;
                                }
                                else  // 群聊消息
                                {
                                    cout << "群组[" << js["groupid"] << "]消息:" << js["time"].get<string>() << " [" << js["id"] << "]" 
                                         << js["name"].get<string>() << " said: " << js["msg"].get<string>() << endl;
                                }
                            }
                        }

                        // 登录成功, 启动接收线程负责接收数据, 需要将clientfd套接字也传给它
                        // 该线程只启动一次
                        static int threadnumber = 0;
                        if (threadnumber == 0)
                        {
                            std::thread readTask(readTaskHandler, clientfd);
                            readTask.detach(); // 设置分离线程,线程运行完资源自动回收
                            threadnumber++;
                        }

                        // 进入聊天主菜单页面
                        isMainMenuRunning = true; // 将全局变量设为true
                        mainMenu(clientfd);       // 由于主菜单页面涉及数据的发送, 要将clientfd套接字传给主页面函数
                    }
                }
            }
        }
        break;
        case 2: // register业务
        {
            char name[50] = {0};
            char pwd[50] = {0};
            cout << "username:";
            cin.getline(name, 50);
            cout << "userpassword:";
            cin.getline(pwd, 50);

            json js;
            js["msgId"] = REG_MSG;
            js["name"] = name;
            js["password"] = pwd;
            string request = js.dump();

            int len = send(clientfd, request.c_str(), strlen(request.c_str()) + 1, 0);
            if (len == -1)
            {
                cerr << "send reg msg error:" << request << endl;
            }
            else
            {
                char buffer[1024] = {0};
                len = recv(clientfd, buffer, 1024, 0);
                if (-1 == len)
                {
                    cerr << "recv reg response error" << endl;
                }
                else
                {
                    json responsejs = json::parse(buffer);
                    if (responsejs["errno"].get<int>() != 0) // 错误号为1,注册失败
                    {
                        cerr << name << " is already exist, register error!" << endl;
                    }
                    else // 错误号为0,注册成功
                    {
                        cout << name << " register success, userid is " << responsejs["id"]
                             << ", do not forget it!" << endl;
                    }
                }
            }
        }
        break;
        case 3:              // quit业务
            close(clientfd); // 释放资源
            exit(0);
        default:
            cerr << "invalid input!" << endl;
            break;
        }
    }

    return 0;
}

// 接收线程
void readTaskHandler(int clientfd)
{
    for (;;)
    {
        char buffer[1024] = {0};                   // 存储接收的数据
        int len = recv(clientfd, buffer, 1024, 0); // 阻塞了
        if (-1 == len || 0 == len)
        {
            close(clientfd);
            exit(-1);
        }

        // 接收ChatServer转发的数据，反序列化生成json数据对象
        json js = json::parse(buffer);
        int msgtype = js["msgId"].get<int>();
        if (ONE_CHAT_MSG == msgtype)   // 一对一聊天
        {
            cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                 << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
        
        if (GROUP_CHAT_MSG == msgtype) // 群聊
        {
            cout << "群组[" << js["groupid"] << "]消息:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                 << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
    }
}

// 显示当前登录成功用户的基本信息
void showCurrentUserData()
{
    cout << "======================login user======================" << endl;
    cout << "current login user => id:" << g_currentUser.getId() << " name:" << g_currentUser.getName() << endl;
    cout << "----------------------friend list---------------------" << endl;
    if (!g_currentUserFriendList.empty()) // 当前用户有好友
    {
        for (User &user : g_currentUserFriendList) // 输出所有好友的信息
        {
            cout << user.getId() << " " << user.getName() << " " << user.getState() << endl;
        }
    }
    cout << "----------------------group list----------------------" << endl;
    if (!g_currentUserGroupList.empty()) // 当前用户有群组
    {
        for (Group &group : g_currentUserGroupList) // 输出所有群组的信息
        {
            cout << group.getId() << " " << group.getName() << " " << group.getDesc() << endl;
            for (GroupUser &user : group.getUsers()) // 输出群组里所有成员的信息
            {
                cout << user.getId() << " " << user.getName() << " " << user.getState()
                     << " " << user.getRole() << endl;
            }
            cout << "——————————————————————————————————————————————————————" << endl;
        }
    }
    cout << "======================================================" << endl;
}

// "help" command handler
void help(int fd = 0, string str = "");
// "chat" command handler
void chat(int, string);
// "addfriend" command handler
void addfriend(int, string);
// "creategroup" command handler
void creategroup(int, string);
// "addgroup" command handler
void addgroup(int, string);
// "groupchat" command handler
void groupchat(int, string);
// "loginout" command handler
void loginout(int, string);

// 系统支持的客户端命令列表
unordered_map<string, string> commandMap = {
    {"help", "显示所有支持的命令, 格式help"},
    {"chat", "一对一聊天, 格式chat:friendid:message"},
    {"addfriend", "添加好友, 格式addfriend:friendid"},
    {"creategroup", "创建群组, 格式creategroup:groupname:groupdesc"},
    {"addgroup", "加入群组, 格式addgroup:groupid"},
    {"groupchat", "群聊, 格式groupchat:groupid:message"},
    {"loginout", "注销, 格式loginout"}
};

// 注册系统支持的客户端命令处理
// function<void(int, string)> int型参数是传clientfd, string型参数是传用户输入的数据
unordered_map<string, function<void(int, string)>> commandHandlerMap = {
    {"help", help},               // 显示所有支持的命令
    {"chat", chat},               // 聊天
    {"addfriend", addfriend},     // 添加好友
    {"creategroup", creategroup}, // 创建群组
    {"addgroup", addgroup},       // 添加群组
    {"groupchat", groupchat},     // 群组聊天
    {"loginout", loginout}        // 用户注销
};

// 主聊天页面程序
void mainMenu(int clientfd)
{
    help(); // 输出系统支持的所有命令信息

    char buffer[1024] = {0};
    while (isMainMenuRunning)
    {
        cin.getline(buffer, 1024); // 获取用户输入的字符串
        string commandbuf(buffer); // 将buffer字符数组转换为string变量commandbuf
        string command; // 存储命令

        // 根据上面的命令列表commandMap, help和loginout的格式里是没有冒号的, 其他命令都含有冒号
        int idx = commandbuf.find(":"); // 在commandbuf字符串中寻找冒号
        if (-1 == idx) // 若找不到冒号, 只可能是 help 或 loginout 命令
        {
            // command = "help" 或 command = "loginout"
            command = commandbuf;
        }
        else // 找到第一个冒号, command赋值为命令名 (下标为[0, idx-1])
        {
            command = commandbuf.substr(0, idx);
        }
        auto it = commandHandlerMap.find(command); // 在commandHandlerMap查找命令command对应的命令处理器
        if (it == commandHandlerMap.end()) // 找不到该命令对应的处理函数, 提示错误
        {
            cerr << "invalid input command!" << endl;
            continue; // 用户重新输入
        }

        // 调用相应命令的事件处理回调, mainMenu对修改封闭，添加新功能不需要修改该函数 (开闭原则)
        // 第一个参数为套接字clientfd, 第二个参数为除命令名外剩下的字符串数据
        it->second(clientfd, commandbuf.substr(idx + 1, commandbuf.size() - idx)); // 调用命令处理方法
    }
}

// "help" command handler
void help(int, string)
{
    cout << "show command list >>> " << endl;
    for (auto &p : commandMap) // 打印系统所有支持的命令
    {
        // p.first: 命令名  p.second: 命令的注释信息
        cout << p.first << " : " << p.second << endl;
    }
    cout << endl;
}

// "addfriend" command handler
void addfriend(int clientfd, string str)
{
    int friendid = atoi(str.c_str()); // 参数str传的是friendid字符串, 先将它转为整型
    json js;
    js["msgId"] = ADD_FRIEND_MSG;
    js["id"] = g_currentUser.getId(); // 获取当前用户id
    js["friendid"] = friendid;        // 待添加的好友id
    string buffer = js.dump();        // 序列化json数据

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0); // 发送数据
    if (-1 == len)
    {
        cerr << "send addfriend msg error -> " << buffer << endl;
    }
}

// "chat" command handler
void chat(int clientfd, string str)
{
    int idx = str.find(":"); // 字符串str传的是"friendid:message"
    if (-1 == idx) // 若找不到冒号, 说明chat命令数据格式输入有误
    {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, idx).c_str());        // 分离friendid部分, 并转为整型
    string message = str.substr(idx + 1, str.size() - idx); // 分离message部分

    json js;
    js["msgId"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();     // 获取当前用户id
    js["name"] = g_currentUser.getName(); // 获取当前用户名
    js["toid"] = friendid;                // 聊天的好友id
    js["msg"] = message;                  // 聊天内容
    js["time"] = getCurrentTime();        // 获取发送消息的当前时间
    string buffer = js.dump();            // 序列化json数据

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0); // 发送数据
    if (-1 == len)
    {
        cerr << "send chat msg error -> " << buffer << endl;
    }
}

// "creategroup" command handler  groupname:groupdesc
void creategroup(int clientfd, string str)
{
    int idx = str.find(":"); // 字符串str传的是"groupname:groupdesc"
    if (-1 == idx)
    {
        cerr << "creategroup command invalid!" << endl;
        return;
    }

    string groupname = str.substr(0, idx);                    // 分离groupname部分
    string groupdesc = str.substr(idx + 1, str.size() - idx); // 分离groupdesc部分

    json js;
    js["msgId"] = CREATE_GROUP_MSG;
    js["id"] = g_currentUser.getId(); // 获取群组创建人id
    js["groupname"] = groupname;      // 群组名
    js["groupdesc"] = groupdesc;      // 群组描述
    string buffer = js.dump();        // 序列化json数据
    
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0); // 发送数据
    if (-1 == len)
    {
        cerr << "send creategroup msg error -> " << buffer << endl;
    }
}

// "addgroup" command handler
void addgroup(int clientfd, string str)
{
    int groupid = atoi(str.c_str()); // 参数str传的是"groupid", 转为整型
    json js;
    js["msgId"] = ADD_GROUP_MSG;
    js["id"] = g_currentUser.getId();
    js["groupid"] = groupid;
    string buffer = js.dump();

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len)
    {
        cerr << "send addgroup msg error -> " << buffer << endl;
    }
}

// "groupchat" command handler   groupid:message
void groupchat(int clientfd, string str)
{
    int idx = str.find(":"); // 参数str传的是"groupid:message"
    if (-1 == idx)
    {
        cerr << "groupchat command invalid!" << endl;
        return;
    }

    int groupid = atoi(str.substr(0, idx).c_str());         // 分离groupid部分, 转为整型
    string message = str.substr(idx + 1, str.size() - idx); // 分离message部分

    json js;
    js["msgId"] = GROUP_CHAT_MSG;
    js["id"] = g_currentUser.getId();     // 获取当前用户id
    js["name"] = g_currentUser.getName(); // 获取当前用户名
    js["groupid"] = groupid;              // 获取群组id
    js["msg"] = message;                  // 获取当前用户发送的群聊消息内容
    js["time"] = getCurrentTime();        // 获取发送消息的时间
    string buffer = js.dump();            // 序列化json数据

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0); // 发送数据
    if (-1 == len)
    {
        cerr << "send groupchat msg error -> " << buffer << endl;
    }
}

// "loginout" command handler
void loginout(int clientfd, string str)
{
    json js;
    js["msgId"] = LOGINOUT_MSG;
    js["id"] = g_currentUser.getId(); // 获取当前要注销的用户id
    string buffer = js.dump();        // 序列化json数据

    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0); // 发送数据
    if (-1 == len)
    {
        cerr << "send loginout msg error -> " << buffer << endl;
    }
    else
    {
        isMainMenuRunning = false; // 将全局变量设为false, 回到首页面
    }
}

// 获取系统时间（聊天信息需要添加时间信息）
string getCurrentTime()
{
    auto tt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm *ptm = localtime(&tt);
    char date[60] = {0};
    sprintf(date, "%d-%02d-%02d %02d:%02d:%02d",
            (int)ptm->tm_year + 1900, (int)ptm->tm_mon + 1, (int)ptm->tm_mday,
            (int)ptm->tm_hour, (int)ptm->tm_min, (int)ptm->tm_sec);
    return std::string(date);
}
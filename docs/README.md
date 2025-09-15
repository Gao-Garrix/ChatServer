# ChatServer

## 项目概述
ChatServer是一个可以工作在nginx tcp负载均衡环境中的集群聊天服务器和客户端系统。该项目基于muduo网络库实现，使用基于发布-订阅模式的Redis消息队列作为跨服务器通信的中间件，使用MySQL数据库存储用户数据。

## 系统架构

### 整体架构
系统采用C/S架构，服务器端基于muduo网络库，采用事件驱动模型；客户端采用多线程设计，实现用户交互和数据收发。

### 服务器端架构
- **网络层**：基于muduo网络库，实现TCP连接管理、消息收发等底层网络功能
- **业务层**：处理各种业务逻辑，包括用户登录注册、一对一聊天、群组聊天、好友管理、群组管理等
- **数据层**：通过MySQL数据库持久化用户数据，使用Redis实现跨服务器通信

### 客户端架构
- **UI交互层**：提供命令行界面，实现用户交互
- **网络通信层**：与服务端建立TCP连接，收发消息
- **业务逻辑层**：处理用户操作，展示聊天信息等

## 核心模块实现

### 1. 服务器端核心模块

#### 1.1 ChatServer类 (chatserver.hpp/cpp)
**功能**：聊天服务器的主类，负责网络连接管理和事件分发。

**主要接口**：
- `ChatServer(EventLoop* loop, const InetAddress& listenAddr, const string& nameArg)`：初始化聊天服务器对象
- `void start()`：启动服务，将监听文件描述符添加到epoll中
- `void onConnection(const TcpConnectionPtr&)`：处理客户端连接/断开事件
- `void onMessage(const TcpConnectionPtr &con, Buffer *buffer, Timestamp time)`：处理客户端消息

#### 1.2 ChatService类 (chatservice.hpp/cpp)
**功能**：聊天服务器业务类，采用单例模式设计，处理各种业务逻辑。

**主要接口**：
- `static ChatService* instance()`：获取单例对象的接口函数
- `void login(const TcpConnectionPtr &con, json &js, Timestamp time)`：处理用户登录业务
- `void reg(const TcpConnectionPtr &con, json &js, Timestamp time)`：处理用户注册业务
- `void oneChat(const TcpConnectionPtr &con, json &js, Timestamp time)`：处理一对一聊天业务
- `void addFriend(const TcpConnectionPtr &con, json &js, Timestamp time)`：处理添加好友业务
- `void createGroup(const TcpConnectionPtr &con, json &js, Timestamp time)`：处理创建群组业务
- `void addGroup(const TcpConnectionPtr &con, json &js, Timestamp time)`：处理加入群组业务
- `void groupChat(const TcpConnectionPtr &con, json &js, Timestamp time)`：处理群组聊天业务
- `void loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)`：处理用户注销业务
- `void clientCloseException(const TcpConnectionPtr &con)`：处理客户端异常退出
- `void reset()`：服务器异常中断时，业务重置方法
- `MsgHandler getHandler(int msgId)`：获取消息ID对应的业务处理器
- `void handleRedisSubscribeMessage(int userid, string msg)`：处理从Redis消息队列中获取的订阅消息

#### 1.3 数据模型模块 (model/)

##### User类 (user.hpp)
**功能**：User表的ORM类，封装用户数据。

**主要接口**：
- `User(int id=-1, string name="", string pwd="", string state="offline")`：构造方法初始化对象
- `setId(int id)`/`getId()`：设置/获取用户ID
- `setName(string name)`/`getName()`：设置/获取用户名
- `setPwd(string pwd)`/`getPwd()`：设置/获取用户密码
- `setState(string state)`/`getState()`：设置/获取用户状态

##### 其他模型
- **FriendModel**：好友关系模型，管理用户好友列表
- **GroupModel**：群组模型，管理群组信息
- **GroupUser**：群组成员模型，管理群组成员信息
- **OfflineMessageModel**：离线消息模型，管理离线消息

#### 1.4 数据库模块 (db/)

##### MySQL类 (db.h)
**功能**：数据库操作类，封装MySQL连接和基本操作。

**主要接口**：
- `MySQL()`：构造方法，初始化数据库连接
- `~MySQL()`：析构方法，释放数据库连接资源
- `bool connect()`：连接数据库
- `bool update(string sql)`：执行更新操作
- `MYSQL_RES *query(string sql)`：执行查询操作
- `MYSQL* getConnection()`：获取数据库连接

#### 1.5 Redis模块 (redis/)

##### Redis类 (redis.hpp)
**功能**：Redis操作类，实现基于发布-订阅的消息队列功能，用于跨服务器通信。

**主要接口**：
- `Redis()`：构造方法
- `~Redis()`：析构方法
- `bool connect()`：连接Redis服务器
- `bool publish(int channel, string message)`：向Redis指定通道发布消息
- `bool subscribe(int channel)`：向Redis指定通道订阅消息
- `bool unsubscribe(int channel)`：取消订阅Redis指定通道
- `void observer_channel_message()`：在独立线程中接收订阅通道中的消息
- `void init_notify_handler(function<void(int, string)> fn)`：初始化业务层上报通道消息的回调对象

### 2. 客户端核心模块

#### 2.1 主程序 (client/main.cpp)
**功能**：客户端程序入口，处理用户交互和网络通信。

**主要功能**：
- 用户登录/注册
- 好友管理（添加好友、显示好友列表）
- 群组管理（创建群组、加入群组、显示群组列表）
- 聊天功能（一对一聊天、群组聊天）

#### 2.2 通信协议
**功能**：定义客户端与服务端之间的通信协议。

**消息类型**（public.hpp）：
- `LOGIN_MSG`/`LOGIN_MSG_ACK`：登录请求/响应
- `REG_MSG`/`REG_MSG_ACK`：注册请求/响应
- `LOGINOUT_MSG`：用户注销
- `ONE_CHAT_MSG`：一对一聊天消息
- `ADD_FRIEND_MSG`：添加好友请求
- `CREATE_GROUP_MSG`：创建群组请求
- `ADD_GROUP_MSG`：加入群组请求
- `GROUP_CHAT_MSG`：群组聊天消息

## 编译与运行

### 编译方式
```bash
cd build
rm -rf *
cmake ..
make
```

### 运行方式

#### 服务器端
```bash
./ChatServer <ip> <port>
例如：./ChatServer 127.0.0.1 6000
```

#### 客户端
```bash
./ChatClient
```

## 依赖库
- muduo_net
- muduo_base
- mysqlclient
- hiredis
- pthread
- nlohmann/json（第三方库，位于thirdparty目录）

## 项目特点
1. 采用事件驱动模型，高并发性能好
2. 支持集群部署，通过Redis实现跨服务器通信
3. 完整的用户系统，包括登录注册、好友管理、群组管理等功能
4. 支持离线消息
5. 客户端采用多线程设计，保证UI响应和网络通信互不干扰

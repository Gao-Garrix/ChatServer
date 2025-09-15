# 聊天系统改进方案：消息有序性保证与消息撤回功能

## 一、当前系统分析

### 1.1 消息发送机制

当前聊天系统使用以下机制实现用户间消息发送：
1. **一对一聊天**：通过 `oneChat` 函数处理，主要流程如下：
   - 检查接收用户是否在线（在 `_userConnMap` 中）
   - 如果在线，直接发送消息
   - 如果不在线，检查是否在其他服务器上（通过 Redis 发布消息）
   - 如果用户完全离线，将消息存储为离线消息

2. **群组聊天**：通过 `groupChat` 函数处理，主要流程：
   - 查询群组成员列表
   - 遍历每个成员，按一对一聊天的逻辑发送消息

3. **离线消息处理**：通过 `offlineMsgModel` 类实现，提供存储、查询和删除离线消息的功能

### 1.2 当前系统的不足

1. **消息有序性问题**：
   - 当前系统没有保证消息的有序性
   - 对于同一个会话，不同服务器上的消息可能乱序到达
   - 没有消息序号机制来确保接收方按正确顺序显示消息

2. **消息撤回功能缺失**：
   - 当前系统没有实现消息撤回功能
   - 没有消息状态跟踪机制
   - 没有撤回消息的通知机制

## 二、消息有序性保证方案

### 2.1 方案概述

通过引入消息序号机制和消息状态跟踪，确保消息在接收方能够按发送顺序显示。

### 2.2 具体实现方案

#### 2.2.1 修改 public.hpp 文件

添加新的消息类型和相关定义：

```cpp
// 在 EnMsgType 枚举中添加
enum EnMsgType
{
    // ... 现有定义 ...
    RECALL_MSG = 18,  // 消息撤回请求
    RECALL_MSG_ACK,   // 消息撤回响应
    MSG_STATUS_UPDATE // 消息状态更新
};

// 添加消息状态枚举
enum EnMsgStatus
{
    MSG_STATUS_SENDING = 0,  // 发送中
    MSG_STATUS_SENT = 1,     // 已发送
    MSG_STATUS_DELIVERED = 2,// 已送达
    MSG_STATUS_READ = 3,     // 已读
    MSG_STATUS_RECALLED = 4  // 已撤回
};
```

#### 2.2.2 修改 chatservice.hpp 文件

在 ChatService 类中添加成员变量和方法：

```cpp
private:
    // 添加成员变量
    std::unordered_map<int, int64_t> _userMsgSeqMap;  // 用户最后一条消息的序号
    std::mutex _msgSeqMutex;                          // 保护消息序号的互斥锁

    // 添加消息序号相关方法
    int64_t generateMsgSeq(int userid);                // 生成用户消息序号
    bool checkMsgOrder(int userid, int64_t seq);       // 检查消息顺序

    // 添加消息状态更新方法
    void updateMsgStatus(int64_t msgSeq, EnMsgStatus status);
```

#### 2.2.3 修改 chatservice.cpp 文件

1. 在构造函数中初始化消息序号互斥锁：

```cpp
ChatService::ChatService()
{
    // ... 现有初始化代码 ...

    // 初始化消息序号互斥锁
    _msgSeqMutex = std::mutex();
}
```

2. 添加生成消息序号的方法：

```cpp
// 生成用户消息序号
int64_t ChatService::generateMsgSeq(int userid)
{
    lock_guard<mutex> lock(_msgSeqMutex);
    int64_t seq = ++_userMsgSeqMap[userid];  // 用户序号自增
    return seq;
}

// 检查消息顺序
bool ChatService::checkMsgOrder(int userid, int64_t seq)
{
    lock_guard<mutex> lock(_msgSeqMutex);
    int64_t lastSeq = _userMsgSeqMap[userid];
    return (seq == lastSeq + 1);  // 序号必须是连续的
}
```

3. 修改 oneChat 函数，添加消息序号处理：

```cpp
void ChatService::oneChat(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int toid = js["toid"].get<int>(); // 获取消息接收者的id

    // 添加发送者id
    int fromid = js["id"].get<int>();

    // 生成消息序号
    int64_t msgSeq = generateMsgSeq(fromid);
    js["msgSeq"] = msgSeq;  // 添加消息序号到JSON中

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid); // 在用户连接表中找toid用户的连接
        if (it != _userConnMap.end()) // 在本服务器中找到了该toid用户连接
        {
            // 该toid用户在线, 服务器主动推送消息给接收者
            it->second->send(js.dump());

            // 更新消息状态为已送达
            updateMsgStatus(msgSeq, MSG_STATUS_DELIVERED);
            return;
        }
    }

    // 在本服务器中没找到用户toid的连接, 则在数据库查询用户toid是否在线
    User user = _userModel.query(toid);
    if (user.getState() == "online") // 用户toid在线, 表示用户toid在其它服务器上登录了
    {
        _redis.publish(toid, js.dump()); // 将该消息发送到redis的toid通道

        // 更新消息状态为已送达
        updateMsgStatus(msgSeq, MSG_STATUS_DELIVERED);
        return;
    }

    // 用户toid不在线,存储离线消息
    _offlineMsgModel.insert(toid, js.dump());

    // 更新消息状态为已发送
    updateMsgStatus(msgSeq, MSG_STATUS_SENT);
}
```

4. 修改 groupChat 函数，添加消息序号处理：

```cpp
void ChatService::groupChat(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();                                     // 发送群聊消息的用户id
    int groupid = js["groupid"].get<int>();                               // 用户发送群聊消息所在的群组id

    // 生成消息序号
    int64_t msgSeq = generateMsgSeq(userid);
    js["msgSeq"] = msgSeq;  // 添加消息序号到JSON中

    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid); // 查询该用户userid所在群组groupid的其他用户的id

    lock_guard<mutex> lock(_connMutex); // 加互斥锁,保证操作_userConnMap的线程安全
    for (int id : useridVec)            // 向群组groupid中的其他用户转发用户userid发送的群聊消息
    {
        auto it = _userConnMap.find(id);
        if (it != _userConnMap.end()) // 在本服务器找到了该用户id的连接,即该用户在线
        {
            // 转发群消息
            it->second->send(js.dump());

            // 更新消息状态为已送达
            updateMsgStatus(msgSeq, MSG_STATUS_DELIVERED);
        }
        else
        {
            // 在本服务器中没找到用户id的连接, 则在数据库查询用户id是否在线
            User user = _userModel.query(id);
            if (user.getState() == "online") // 用户id在线, 表示用户id在其它服务器上登录了
            {
                _redis.publish(id, js.dump()); // 将该消息发送到redis的id通道

                // 更新消息状态为已送达
                updateMsgStatus(msgSeq, MSG_STATUS_DELIVERED);
            }
            else
            {
                // 存储离线群消息
                _offlineMsgModel.insert(id, js.dump());
            }
        }
    }

    // 更新发送者的消息状态为已送达
    updateMsgStatus(msgSeq, MSG_STATUS_DELIVERED);
}
```

5. 添加更新消息状态的方法：

```cpp
// 更新消息状态
void ChatService::updateMsgStatus(int64_t msgSeq, EnMsgStatus status)
{
    // 这里可以添加将消息状态存储到数据库的逻辑
    // 可以创建一个新的消息状态表来跟踪每条消息的状态
    // 例如：msg_status (msg_seq, from_id, to_id, status, update_time)

    // 示例SQL（需要创建对应的数据表）：
    // char sql[1024] = {0};
    // sprintf(sql, "INSERT INTO msg_status (msg_seq, status, update_time) VALUES (%ld, %d, NOW()) ON DUPLICATE KEY UPDATE status=%d, update_time=NOW()", 
    //         msgSeq, status, status);

    // MySQL mysql;
    // if (mysql.connect()) {
    //     mysql.update(sql);
    // }
}
```

#### 2.2.4 修改客户端代码

客户端需要做相应修改，处理消息序号和显示顺序：

1. 在客户端存储每个会话的最后接收到的消息序号
2. 当接收到消息时，检查序号是否连续
3. 如果序号不连续，请求缺失的消息或等待其他消息到达
4. 根据序号对消息进行排序后再显示

## 三、消息撤回功能实现方案

### 3.1 方案概述

通过添加消息撤回请求处理逻辑，实现发送方撤回已发送消息的功能。

### 3.2 具体实现方案

#### 3.2.1 修改 public.hpp 文件

添加新的消息类型（已在消息有序性方案中添加）：

```cpp
enum EnMsgType
{
    // ... 现有定义 ...
    RECALL_MSG = 18,  // 消息撤回请求
    RECALL_MSG_ACK,   // 消息撤回响应
    MSG_STATUS_UPDATE // 消息状态更新
};
```

#### 3.2.2 修改 chatservice.hpp 文件

添加消息撤回相关方法：

```cpp
public:
    // 处理消息撤回
    void recallMessage(const TcpConnectionPtr &con, json &js, Timestamp time);

    // 撤回消息到所有接收方
    void recallToAll(int64_t msgSeq, int fromId, const vector<int>& toIds);

private:
    // 添加成员变量
    std::unordered_map<int64_t, int> _msgToFromMap;  // 消息序号到发送者的映射
    std::unordered_map<int64_t, vector<int>> _msgToRecipientsMap;  // 消息序号到接收者的映射
    std::mutex _msgMapMutex;  // 保护消息映射的互斥锁

    // 添加消息映射方法
    void addMessageMapping(int64_t msgSeq, int fromId, const vector<int>& toIds);
    vector<int> getMessageRecipients(int64_t msgSeq);
```

#### 3.2.3 修改 chatservice.cpp 文件

1. 在构造函数中初始化消息映射互斥锁：

```cpp
ChatService::ChatService()
{
    // ... 现有初始化代码 ...

    // 初始化消息映射互斥锁
    _msgMapMutex = std::mutex();
}
```

2. 添加消息映射相关方法：

```cpp
// 添加消息映射
void ChatService::addMessageMapping(int64_t msgSeq, int fromId, const vector<int>& toIds)
{
    lock_guard<mutex> lock(_msgMapMutex);
    _msgToFromMap[msgSeq] = fromId;
    _msgToRecipientsMap[msgSeq] = toIds;
}

// 获取消息接收者
vector<int> ChatService::getMessageRecipients(int64_t msgSeq)
{
    lock_guard<mutex> lock(_msgMapMutex);
    auto it = _msgToRecipientsMap.find(msgSeq);
    if (it != _msgToRecipientsMap.end()) {
        return it->second;
    }
    return vector<int>();
}
```

3. 在 oneChat 和 groupChat 函数中添加消息映射记录：

```cpp
// 在 oneChat 函数中，发送消息前添加
vector<int> recipients = {toid};
addMessageMapping(msgSeq, fromid, recipients);

// 在 groupChat 函数中，发送消息前添加
vector<int> recipients = useridVec;
addMessageMapping(msgSeq, userid, recipients);
```

4. 添加消息撤回处理函数：

```cpp
// 处理消息撤回
void ChatService::recallMessage(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int64_t msgSeq = js["msgSeq"].get<int64_t>();  // 要撤回的消息序号
    int fromId = js["id"].get<int>();              // 发起撤回的用户ID

    // 获取消息接收者列表
    vector<int> recipients = getMessageRecipients(msgSeq);

    // 验证撤回权限：只有消息发送者可以撤回自己的消息
    lock_guard<mutex> lock(_msgMapMutex);
    auto it = _msgToFromMap.find(msgSeq);
    if (it == _msgToFromMap.end() || it->second != fromId) {
        // 消息不存在或不是发送者，返回错误响应
        json response;
        response["msgId"] = RECALL_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "No permission to recall this message";
        con->send(response.dump());
        return;
    }

    // 撤回消息到所有接收方
    recallToAll(msgSeq, fromId, recipients);

    // 返回成功响应
    json response;
    response["msgId"] = RECALL_MSG_ACK;
    response["errno"] = 0;
    response["msgSeq"] = msgSeq;
    con->send(response.dump());
}

// 撤回消息到所有接收方
void ChatService::recallToAll(int64_t msgSeq, int fromId, const vector<int>& toIds)
{
    // 构建撤回消息
    json recallMsg;
    recallMsg["msgId"] = RECALL_MSG;
    recallMsg["fromId"] = fromId;
    recallMsg["msgSeq"] = msgSeq;

    // 遍历所有接收者，发送撤回通知
    lock_guard<mutex> lock(_connMutex);
    for (int toId : toIds) {
        auto it = _userConnMap.find(toId);
        if (it != _userConnMap.end()) {
            // 接收者在线，直接发送撤回消息
            it->second->send(recallMsg.dump());

            // 更新消息状态为已撤回
            updateMsgStatus(msgSeq, MSG_STATUS_RECALLED);
        } else {
            // 接收者不在线，检查是否在其他服务器上
            User user = _userModel.query(toId);
            if (user.getState() == "online") {
                // 通过Redis发送撤回消息
                _redis.publish(toId, recallMsg.dump());

                // 更新消息状态为已撤回
                updateMsgStatus(msgSeq, MSG_STATUS_RECALLED);
            } else {
                // 接收者离线，将撤回消息作为离线消息存储
                // 这里需要修改offlineMsgModel以支持存储撤回消息
                // 可以添加一个字段标识消息类型（普通消息或撤回消息）
                _offlineMsgModel.insert(toId, recallMsg.dump());

                // 更新消息状态为已撤回
                updateMsgStatus(msgSeq, MSG_STATUS_RECALLED);
            }
        }
    }
}
```

5. 修改 handleRedisSubscribeMessage 函数，处理撤回消息：

```cpp
// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end()) // 找到用户userid的连接
    {
        // 解析消息
        json js = json::parse(msg);
        int msgId = js["msgId"].get<int>();

        if (msgId == RECALL_MSG) {
            // 处理撤回消息
            it->second->send(msg);

            // 更新消息状态为已撤回
            int64_t msgSeq = js["msgSeq"].get<int64_t>();
            updateMsgStatus(msgSeq, MSG_STATUS_RECALLED);
        } else {
            // 普通消息
            it->second->send(msg);
        }
        return;
    }

    // 若用户userid下线了, 存储离线消息
    _offlineMsgModel.insert(userid, msg);
}
```

#### 3.2.4 修改 offlinemessagemodel.hpp 和 offlinemessagemodel.cpp

扩展离线消息模型，支持存储撤回消息：

```cpp
// 在 offlinemessagemodel.hpp 中修改
class offlineMsgModel
{
public:
    // 存储用户的离线消息
    void insert(int userid, string msg);

    // 删除用户的离线消息
    void remove(int userid);

    // 查询用户的离线消息,可能有多条,查到的消息用vector容器存储
    vector<string> query(int userid);

    // 修改删除方法，支持按消息序号删除特定消息
    void removeMessage(int userid, int64_t msgSeq);
};
```

```cpp
// 在 offlinemessagemodel.cpp 中添加
// 删除特定序号的离线消息
void offlineMsgModel::removeMessage(int userid, int64_t msgSeq)
{
    // 组装删除语句
    char sql[1024] = {0};
    sprintf(sql, "delete from offlinemessage where userid = %d and message LIKE '%%"msgSeq":%ld%%'", 
            userid, msgSeq);

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        mysql.update(sql); // 执行删除
    }
}
```

#### 3.2.5 数据库表结构更新

需要添加消息状态表来跟踪消息状态：

```sql
CREATE TABLE IF NOT EXISTS msg_status (
    msg_seq BIGINT PRIMARY KEY,
    from_id INT NOT NULL,
    status INT NOT NULL DEFAULT 0,
    create_time DATETIME NOT NULL,
    update_time DATETIME NOT NULL,
    INDEX idx_from_id (from_id),
    INDEX idx_status (status)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

## 四、实施方案总结

### 4.1 实施步骤

1. **数据库更新**：
   - 创建消息状态表 `msg_status`
   - 修改离线消息表结构（可选，用于支持撤回消息的存储）

2. **代码修改**：
   - 更新 `public.hpp` 添加新的消息类型和状态枚举
   - 修改 `chatservice.hpp` 和 `chatservice.cpp` 添加消息序号和撤回功能
   - 修改 `offlinemessagemodel.hpp` 和 `offlinemessagemodel.cpp` 支持撤回消息
   - 更新客户端代码处理消息序号和撤回通知

3. **测试验证**：
   - 测试消息有序性：确保消息按正确顺序显示
   - 测试消息撤回：验证发送方可撤回消息，接收方可看到撤回通知
   - 测试跨服务器场景：确保不同服务器上的用户也能正确接收有序消息和撤回通知

### 4.2 注意事项

1. **消息序号生成**：确保消息序号在全局范围内唯一，可以使用时间戳+随机数+用户ID的组合
2. **性能考虑**：消息状态跟踪会增加数据库写入操作，需要考虑性能优化
3. **存储优化**：对于长期运行的服务器，消息状态表可能会变得很大，需要考虑定期清理旧记录
4. **客户端同步**：确保客户端正确处理消息序号和撤回通知，避免显示问题

### 4.3 扩展可能性

1. **已读回执**：基于消息状态机制，可以扩展实现已读回执功能
2. **消息过期**：可以添加消息过期机制，自动清理过期的消息和状态记录
3. **端到端加密**：可以在消息发送前添加加密，确保消息内容的安全性
4. **多端同步**：扩展支持用户在多个设备上同时在线，并保持消息同步

通过以上方案，可以有效地保证消息的有序性，并实现消息撤回功能，提升聊天系统的用户体验。

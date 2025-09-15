# ChatServer 消息传输幂等性方案

## 1. 幂等性概述

在分布式聊天系统中，由于网络不稳定、客户端重试或服务器故障等情况，同一条消息可能会被多次发送和处理。如果不采取措施，这会导致消息重复、状态不一致等问题。幂等性是指对同一操作执行一次和多次执行的结果相同，是解决这类问题的关键技术。

## 2. 当前系统分析

当前系统的主要消息处理流程包括：

1. **一对一聊天** (`oneChat`): 消息发送者 -> 接收者(在线/离线)
2. **群组聊天** (`groupChat`): 消息发送者 -> 群组内所有成员(在线/离线)
3. **Redis消息队列**: 用于跨服务器通信
4. **离线消息存储**: 用于存储接收者不在线时的消息

当前系统缺乏消息去重机制，可能导致消息重复。

## 3. 幂等性实现方案

### 3.1 消息唯一标识

#### 3.1.1 方案描述
为每条消息生成全局唯一标识，用于识别和去重。

#### 3.1.2 实现步骤

1. **修改 `public.hpp`**，添加消息唯一标识相关常量：
```cpp
// 在 EnMsgType 枚举后添加
enum EnMsgIdempotency
{
    MSG_IDEMPOTENCY_KEY = "msg_idempotency_key", // 消息幂等性标识字段名
    MSG_TIMESTAMP_KEY = "msg_timestamp_key",     // 消息时间戳字段名
    MSG_MAX_RETRY_TIMES = 3,                    // 最大重试次数
};
```

2. **修改 `chatservice.hpp`**，添加消息ID生成和验证方法：
```cpp
// 在 ChatService 类中添加
private:
    // 生成消息唯一ID
    string generateMsgId(const TcpConnectionPtr &con, json &js);

    // 验证消息是否已处理过
    bool isMsgProcessed(const string &msgId);

    // 记录已处理的消息ID
    void recordMsgId(const string &msgId);
```

3. **修改 `chatservice.cpp`**，实现消息ID生成和验证逻辑：
```cpp
// 生成消息唯一ID
string ChatService::generateMsgId(const TcpConnectionPtr &con, json &js)
{
    // 获取当前时间戳
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // 获取发送者ID
    int fromId = js["id"].get<int>();

    // 组合生成唯一ID: 发送者ID_时间戳_随机数
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 9999);
    int randomNum = dis(gen);

    return std::to_string(fromId) + "_" + std::to_string(timestamp) + "_" + std::to_string(randomNum);
}

// 验证消息是否已处理过
bool ChatService::isMsgProcessed(const string &msgId)
{
    // 使用Redis的SETNX实现原子性检查
    string key = "processed_msg:" + msgId;
    return _redis.setnx(key, "1") == 0; // 返回0表示key已存在
}

// 记录已处理的消息ID
void ChatService::recordMsgId(const string &msgId)
{
    // 使用Redis存储已处理的消息ID，设置过期时间防止无限增长
    string key = "processed_msg:" + msgId;
    _redis.setex(key, 86400, "1"); // 24小时后过期
}
```

### 3.2 消息处理状态记录

#### 3.2.1 方案描述
记录消息的处理状态，防止重复处理。

#### 3.2.2 实现步骤

1. **修改 `public.hpp`**，添加消息状态相关常量：
```cpp
// 在 EnMsgIdempotency 枚举后添加
enum EnMsgStatus
{
    MSG_STATUS_PENDING = "pending",    // 消息待处理
    MSG_STATUS_PROCESSING = "processing", // 消息处理中
    MSG_STATUS_COMPLETED = "completed", // 消息已处理完成
    MSG_STATUS_FAILED = "failed",      // 消息处理失败
};
```

2. **修改 `offlinemessagemodel.hpp`**，添加消息状态字段：
```cpp
// 修改表结构，添加消息状态字段
// 现有表结构: CREATE TABLE offlinemessage(userid INT, message TEXT);
// 新表结构: CREATE TABLE offlinemessage(
//     id INT AUTO_INCREMENT PRIMARY KEY,
//     userid INT NOT NULL,
//     message TEXT NOT NULL,
//     msg_id VARCHAR(64) NOT NULL,
//     status VARCHAR(20) NOT NULL DEFAULT 'pending',
//     create_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
// );
```

3. **修改 `offlinemessagemodel.cpp`**，更新相关方法：
```cpp
// 修改 insert 方法，添加消息ID和状态
void offlineMsgModel::insert(int userid, string msg, string msgId)
{
    char sql[1024] = {0};
    sprintf(sql, "insert into offlinemessage(userid, message, msg_id, status) values(%d, '%s', '%s', 'pending')", 
            userid, msg.c_str(), msgId.c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}

// 添加更新消息状态的方法
bool offlineMsgModel::updateMsgStatus(string msgId, string status)
{
    char sql[1024] = {0};
    sprintf(sql, "update offlinemessage set status = '%s' where msg_id = '%s'", status.c_str(), msgId.c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        return mysql.update(sql);
    }
    return false;
}

// 修改 query 方法，只返回状态为"pending"的消息
vector<string> offlineMsgModel::query(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "select message from offlinemessage where userid = %d and status = 'pending'", userid);

    vector<string> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while((row = mysql_fetch_row(res)) != nullptr)
            {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}
```

### 3.3 消息重试机制

#### 3.3.1 方案描述
实现消息重试机制，确保消息可靠传递。

#### 3.3.2 实现步骤

1. **修改 `chatservice.hpp`**，添加重试相关方法：
```cpp
// 在 ChatService 类中添加
private:
    // 消息重试计数
    void incrementMsgRetryCount(const string &msgId);

    // 获取消息重试次数
    int getMsgRetryCount(const string &msgId);

    // 检查是否超过最大重试次数
    bool isMaxRetryExceeded(const string &msgId);
```

2. **修改 `chatservice.cpp`**，实现重试逻辑：
```cpp
// 消息重试计数
void ChatService::incrementMsgRetryCount(const string &msgId)
{
    string key = "msg_retry_count:" + msgId;
    _redis.incr(key);
    _redis.expire(key, 86400); // 24小时后过期
}

// 获取消息重试次数
int ChatService::getMsgRetryCount(const string &msgId)
{
    string key = "msg_retry_count:" + msgId;
    string count = _redis.get(key);
    if (count.empty())
    {
        return 0;
    }
    return std::stoi(count);
}

// 检查是否超过最大重试次数
bool ChatService::isMaxRetryExceeded(const string &msgId)
{
    return getMsgRetryCount(msgId) >= MSG_MAX_RETRY_TIMES;
}
```

3. **修改 `oneChat` 方法**，添加幂等性检查：
```cpp
// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    // 生成消息唯一ID
    string msgId = generateMsgId(con, js);
    js[MSG_IDEMPOTENCY_KEY] = msgId; // 将消息ID添加到JSON中

    // 检查消息是否已处理过
    if (isMsgProcessed(msgId))
    {
        // 记录重试次数
        incrementMsgRetryCount(msgId);

        // 如果超过最大重试次数，记录错误日志
        if (isMaxRetryExceeded(msgId))
        {
            LOG_ERROR << "Message " << msgId << " exceeded max retry times";
            return;
        }

        // 如果是重复消息但未超过重试次数，直接返回
        return;
    }

    int toid = js["toid"].get<int>(); // 获取消息接收者的id

    // 标记消息为处理中状态
    _offlineMsgModel.updateMsgStatus(msgId, "processing");

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid); // 在用户连接表中找toid用户的连接
        if (it != _userConnMap.end()) // 在本服务器中找到了该toid用户连接
        {
            // 该toid用户在线, 服务器主动推送消息给接收者
            it->second->send(js.dump());

            // 标记消息为已完成状态
            _offlineMsgModel.updateMsgStatus(msgId, "completed");

            // 记录消息ID
            recordMsgId(msgId);
            return;
        }
    }

    // 在本服务器中没找到用户toid的连接, 则在数据库查询用户toid是否在线
    User user = _userModel.query(toid);
    if (user.getState() == "online") // 用户toid在线, 表示用户toid在其它服务器上登录了
    {
        _redis.publish(toid, js.dump()); // 将该消息发送到redis的toid通道

        // 标记消息为已完成状态
        _offlineMsgModel.updateMsgStatus(msgId, "completed");

        // 记录消息ID
        recordMsgId(msgId);
        return;
    }

    // 用户toid不在线,存储离线消息
    _offlineMsgModel.insert(toid, js.dump(), msgId);

    // 标记消息为已完成状态
    _offlineMsgModel.updateMsgStatus(msgId, "completed");

    // 记录消息ID
    recordMsgId(msgId);
}
```

4. **修改 `groupChat` 方法**，添加类似的幂等性检查：
```cpp
// 群组聊天业务
void ChatService::groupChat(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    // 生成消息唯一ID
    string msgId = generateMsgId(con, js);
    js[MSG_IDEMPOTENCY_KEY] = msgId; // 将消息ID添加到JSON中

    // 检查消息是否已处理过
    if (isMsgProcessed(msgId))
    {
        // 记录重试次数
        incrementMsgRetryCount(msgId);

        // 如果超过最大重试次数，记录错误日志
        if (isMaxRetryExceeded(msgId))
        {
            LOG_ERROR << "Message " << msgId << " exceeded max retry times";
            return;
        }

        // 如果是重复消息但未超过重试次数，直接返回
        return;
    }

    int userid = js["id"].get<int>();                                     // 发送群聊消息的用户id
    int groupid = js["groupid"].get<int>();                               // 用户发送群聊消息所在的群组id
    vector<int> useridVec = _groupModel.queryGroupUsers(userid, groupid); // 查询该用户userid所在群组groupid的其他用户的id

    // 标记消息为处理中状态
    _offlineMsgModel.updateMsgStatus(msgId, "processing");

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
                _offlineMsgModel.insert(id, js.dump(), msgId);
            }
        }
    }

    // 标记消息为已完成状态
    _offlineMsgModel.updateMsgStatus(msgId, "completed");

    // 记录消息ID
    recordMsgId(msgId);
}
```

### 3.4 Redis消息队列幂等性

#### 3.4.1 方案描述
确保Redis消息队列中的消息不会被重复处理。

#### 3.4.2 实现步骤

1. **修改 `redis.cpp`**，在 `observer_channel_message` 方法中添加幂等性检查：
```cpp
// 在独立线程中接收订阅通道中的消息
void Redis::observer_channel_message()
{
    redisReply *reply = nullptr;

    // redisGetReply从订阅消息上下文以阻塞方式等待redis服务器响应
    while (REDIS_OK == redisGetReply(this->_subcribe_context, (void **)&reply))
    {
        // 订阅收到的消息是一个带三元素的数组 element[2]是消息内容 element[1]是通道号
        if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr)
        {
            // 解析消息内容
            string message = reply->element[2]->str;
            json js = json::parse(message);

            // 检查消息是否包含幂等性ID
            if (js.contains(MSG_IDEMPOTENCY_KEY))
            {
                string msgId = js[MSG_IDEMPOTENCY_KEY].get<string>();

                // 检查消息是否已处理过
                string processedKey = "processed_msg:" + msgId;
                if (redisCommand(_publish_context, "EXISTS %s", processedKey.c_str()) == 1)
                {
                    // 消息已处理过，跳过
                    freeReplyObject(reply);
                    continue;
                }

                // 标记消息为已处理
                redisCommand(_publish_context, "SETEX %s 86400 1", processedKey.c_str());
            }

            // 给业务层上报通道上发生的消息
            _notify_message_handler(atoi(reply->element[1]->str), message);
        }

        freeReplyObject(reply); // 释放redisReply对象占用的内存
    }

    cerr << ">>>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<<" << endl;
}
```

### 3.5 数据库事务处理

#### 3.5.1 方案描述
使用数据库事务确保消息处理的原子性。

#### 3.5.2 实现步骤

1. **修改 `offlinemessagemodel.cpp`**，添加事务支持：
```cpp
// 修改 insert 方法，添加事务支持
void offlineMsgModel::insert(int userid, string msg, string msgId)
{
    MySQL mysql;
    if (mysql.connect())
    {
        // 开启事务
        mysql.query("START TRANSACTION");

        try {
            // 组装插入语句
            char sql[1024] = {0};
            sprintf(sql, "insert into offlinemessage(userid, message, msg_id, status) values(%d, '%s', '%s', 'pending')", 
                    userid, msg.c_str(), msgId.c_str());

            // 执行插入
            mysql.update(sql);

            // 提交事务
            mysql.query("COMMIT");
        } catch (...) {
            // 发生异常，回滚事务
            mysql.query("ROLLBACK");
            throw;
        }
    }
}

// 修改 updateMsgStatus 方法，添加事务支持
bool offlineMsgModel::updateMsgStatus(string msgId, string status)
{
    MySQL mysql;
    if (mysql.connect())
    {
        // 开启事务
        mysql.query("START TRANSACTION");

        try {
            // 组装更新语句
            char sql[1024] = {0};
            sprintf(sql, "update offlinemessage set status = '%s' where msg_id = '%s'", status.c_str(), msgId.c_str());

            // 执行更新
            bool result = mysql.update(sql);

            // 提交事务
            mysql.query("COMMIT");

            return result;
        } catch (...) {
            // 发生异常，回滚事务
            mysql.query("ROLLBACK");
            throw;
        }
    }
    return false;
}
```

## 4. 实施计划

### 4.1 第一阶段：基础设施准备
1. 修改数据库表结构，添加消息ID和状态字段
2. 更新Redis操作类，支持幂等性检查
3. 实现消息ID生成和验证机制

### 4.2 第二阶段：核心功能实现
1. 修改一对一聊天功能，添加幂等性检查
2. 修改群组聊天功能，添加幂等性检查
3. 实现消息重试机制

### 4.3 第三阶段：完善和优化
1. 添加消息处理状态跟踪
2. 实现数据库事务支持
3. 添加日志和监控功能

## 5. 测试方案

### 5.1 功能测试
1. **消息重复发送测试**：模拟网络不稳定情况，多次发送同一条消息，验证是否只处理一次
2. **服务器故障恢复测试**：在消息处理过程中模拟服务器故障，验证重启后是否正确处理消息
3. **跨服务器通信测试**：测试跨服务器的消息传递，确保幂等性在分布式环境下有效

### 5.2 性能测试
1. **消息处理性能**：测试在高并发情况下的消息处理能力
2. **内存使用**：监控消息ID存储的内存使用情况
3. **Redis负载**：测试Redis在处理大量消息时的性能表现

## 6. 风险与对策

### 6.1 风险点
1. **消息ID生成冲突**：虽然概率很低，但仍有可能出现重复的消息ID
2. **Redis故障**：如果Redis服务不可用，会影响幂等性检查
3. **性能影响**：增加的幂等性检查可能会影响消息处理性能

### 6.2 对策
1. **使用更可靠的ID生成算法**：如UUID或Snowflake算法
2. **Redis集群部署**：确保Redis的高可用性
3. **优化幂等性检查**：使用布隆过滤器等数据结构减少内存使用

## 7. 总结

通过实施上述幂等性方案，可以有效解决ChatServer在消息传输过程中可能出现的重复问题，提高系统的可靠性和稳定性。方案分为基础设施、核心功能和完善优化三个阶段实施，确保平滑过渡。同时，通过全面的测试和风险控制，确保方案的可行性和有效性。

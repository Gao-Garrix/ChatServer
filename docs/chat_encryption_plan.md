# 聊天系统加密方案

## 1. 当前系统安全状况分析

### 1.1 安全风险
- 当前聊天系统所有消息均以明文形式传输，包括：
  - 用户登录凭证（用户名、密码）
  - 聊天内容（一对一聊天、群聊）
  - 用户个人信息（好友列表、群组信息）
- 系统未使用任何加密机制，存在以下安全风险：
  - 用户密码可能被窃取
  - 聊天内容可能被监听和篡改
  - 用户隐私无法保障
  - 可能遭受中间人攻击

### 1.2 消息传输流程
当前系统的消息传输流程如下：
1. 客户端将消息序列化为JSON字符串
2. 通过TCP连接直接发送JSON字符串
3. 服务器接收并解析JSON字符串
4. 服务器处理消息后，再次序列化为JSON字符串
5. 服务器将JSON字符串转发给目标客户端

## 2. 加密方案设计

### 2.1 总体架构
采用混合加密方案，结合对称加密和非对称加密的优点：
- 对称加密：用于加密实际传输的聊天内容（AES-256）
- 非对称加密：用于安全地交换对称加密密钥（RSA-2048）
- 哈希算法：用于验证消息完整性（SHA-256）

### 2.2 加密流程
```
客户端发送流程：
1. 生成随机AES密钥（每次会话不同）
2. 使用AES密钥加密聊天内容
3. 使用服务器公钥加密AES密钥
4. 将加密后的AES密钥和加密内容一起发送

服务器接收流程：
1. 使用服务器私钥解密出AES密钥
2. 使用AES密钥解密聊天内容
3. 处理解密后的消息
4. 使用目标客户端的公钥加密新的AES密钥
5. 使用新的AES密钥加密响应内容
6. 将加密后的AES密钥和加密内容一起发送
```

## 3. 具体实现方案

### 3.1 消息格式修改

#### 3.1.1 新增消息字段
修改 `public.hpp`，新增加密相关消息类型和字段：

```cpp
// 在 EnMsgType 枚举中添加
enum EnMsgType
{
    // ... 现有消息类型
    ENCRYPTED_MSG = 100, // 加密消息类型
    KEY_EXCHANGE_MSG,   // 密钥交换消息类型
};

// 新增消息结构定义
struct MessageHeader {
    int msgId;           // 消息类型
    int encrypted;       // 是否加密 (0:明文, 1:加密)
    int algorithm;       // 加密算法 (1:AES, 2:RSA)
    int keyLength;       // 密钥长度
    int contentLength;   // 加密内容长度
    char checksum[65];   // SHA-256校验和(64字符+结束符)
};
```

#### 3.1.2 消息封装结构
设计新的消息封装结构：

```cpp
struct EncryptedMessage {
    MessageHeader header;    // 消息头
    char encryptedKey[256];  // 加密的对称密钥
    char iv[16];            // AES初始化向量
    char encryptedContent[]; // 加密后的内容(变长)
};
```

### 3.2 客户端修改

#### 3.2.1 加密工具类创建
创建 `encryption.cpp` 和 `encryption.hpp` 实现加密功能：

```cpp
// encryption.hpp
#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <string>
#include <vector>
#include <openssl/rsa.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

class Encryption {
public:
    // 生成RSA密钥对
    static std::pair<std::string, std::string> generateRSAKeys();

    // 使用公钥加密
    static std::string encryptWithPublicKey(const std::string& publicKey, const std::string& data);

    // 使用私钥解密
    static std::string decryptWithPrivateKey(const std::string& privateKey, const std::string& encryptedData);

    // 生成随机AES密钥
    static std::string generateAESKey();

    // AES加密
    static std::string encryptAES(const std::string& key, const std::string& iv, const std::string& data);

    // AES解密
    static std::string decryptAES(const std::string& key, const std::string& iv, const std::string& encryptedData);

    // 生成随机IV
    static std::string generateIV();

    // 计算SHA-256哈希
    static std::string calculateSHA256(const std::string& data);

    // 验证哈希
    static bool verifySHA256(const std::string& data, const std::string& hash);
};

#endif // ENCRYPTION_H
```

#### 3.2.2 客户端消息发送修改
修改 `src/client/main.cpp` 中的消息发送函数：

```cpp
// 修改chat函数
void chat(int clientfd, string str)
{
    int idx = str.find(":");
    if (-1 == idx) {
        cerr << "chat command invalid!" << endl;
        return;
    }

    int friendid = atoi(str.substr(0, idx).c_str());
    string message = str.substr(idx + 1, str.size() - idx);

    json js;
    js["msgId"] = ONE_CHAT_MSG;
    js["id"] = g_currentUser.getId();
    js["name"] = g_currentUser.getName();
    js["toid"] = friendid;
    js["msg"] = message;
    js["time"] = getCurrentTime();

    // 新增：加密消息内容
    string aesKey = Encryption::generateAESKey();  // 生成随机AES密钥
    string iv = Encryption::generateIV();          // 生成随机IV
    string encryptedContent = Encryption::encryptAES(aesKey, iv, message);  // 加密消息内容

    // 获取服务器公钥（这里简化处理，实际应该从服务器获取或预先存储）
    string serverPublicKey = getServerPublicKey();  // 需要实现此函数

    // 使用服务器公钥加密AES密钥
    string encryptedKey = Encryption::encryptWithPublicKey(serverPublicKey, aesKey);

    // 构建加密消息
    json encryptedJs;
    encryptedJs["msgId"] = ENCRYPTED_MSG;
    encryptedJs["id"] = g_currentUser.getId();
    encryptedJs["name"] = g_currentUser.getName();
    encryptedJs["toid"] = friendid;
    encryptedJs["encryptedKey"] = encryptedKey;
    encryptedJs["iv"] = iv;
    encryptedJs["encryptedContent"] = encryptedContent;
    encryptedJs["time"] = getCurrentTime();
    encryptedJs["checksum"] = Encryption::calculateSHA256(encryptedContent);  // 添加校验和

    string buffer = encryptedJs.dump();
    int len = send(clientfd, buffer.c_str(), strlen(buffer.c_str()) + 1, 0);
    if (-1 == len) {
        cerr << "send encrypted chat msg error -> " << buffer << endl;
    }
}
```

#### 3.2.3 客户端消息接收修改
修改 `readTaskHandler` 函数：

```cpp
void readTaskHandler(int clientfd)
{
    for (;;) {
        char buffer[1024] = {0};
        int len = recv(clientfd, buffer, 1024, 0);
        if (-1 == len || 0 == len) {
            close(clientfd);
            exit(-1);
        }

        json js = json::parse(buffer);
        int msgtype = js["msgId"].get<int>();

        // 处理加密消息
        if (ENCRYPTED_MSG == msgtype) {
            // 获取加密数据
            string encryptedKey = js["encryptedKey"];
            string iv = js["iv"];
            string encryptedContent = js["encryptedContent"];
            string checksum = js["checksum"];

            // 验证校验和
            if (!Encryption::verifySHA256(encryptedContent, checksum)) {
                cerr << "Message checksum verification failed!" << endl;
                continue;
            }

            // 使用私钥解密AES密钥（这里简化处理，实际应该使用客户端私钥）
            string privateKey = getClientPrivateKey();  // 需要实现此函数
            string aesKey = Encryption::decryptWithPrivateKey(privateKey, encryptedKey);

            // 使用AES密钥解密消息内容
            string message = Encryption::decryptAES(aesKey, iv, encryptedContent);

            // 解析消息内容
            json msgjs = json::parse(message);

            // 显示消息
            cout << msgjs["time"].get<string>() << " [" << msgjs["id"] << "]" << msgjs["name"].get<string>()
                 << " said: " << msgjs["msg"].get<string>() << endl;
            continue;
        }

        // 处理明文消息（向后兼容）
        if (ONE_CHAT_MSG == msgtype) {
            cout << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                 << " said: " << js["msg"].get<string>() << endl;
            continue;
        }

        if (GROUP_CHAT_MSG == msgtype) {
            cout << "群组[" << js["groupid"] << "]消息:" << js["time"].get<string>() << " [" << js["id"] << "]" << js["name"].get<string>()
                 << " said: " << js["msg"].get<string>() << endl;
            continue;
        }
    }
}
```

### 3.3 服务器端修改

#### 3.3.1 加密工具类实现
创建 `src/server/encryption.cpp` 实现加密功能：

```cpp
// encryption.cpp
#include "encryption.hpp"
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <vector>

std::pair<std::string, std::string> Encryption::generateRSAKeys() {
    RSA *rsa = RSA_new();
    BIGNUM *bn = BN_new();
    BN_set_word(bn, RSA_F4);

    RSA_generate_key_ex(rsa, 2048, bn, NULL);

    BIO *bioPublic = BIO_new(BIO_s_mem());
    BIO *bioPrivate = BIO_new(BIO_s_mem());

    PEM_write_bio_RSAPublicKey(bioPublic, rsa);
    PEM_write_bio_RSAPrivateKey(bioPrivate, rsa, NULL, NULL, 0, NULL, NULL);

    char *publicKeyPtr = NULL;
    char *privateKeyPtr = NULL;
    long publicKeyLen = BIO_get_mem_data(bioPublic, &publicKeyPtr);
    long privateKeyLen = BIO_get_mem_data(bioPrivate, &privateKeyPtr);

    std::string publicKey(publicKeyPtr, publicKeyLen);
    std::string privateKey(privateKeyPtr, privateKeyLen);

    BIO_free_all(bioPublic);
    BIO_free_all(bioPrivate);
    RSA_free(rsa);
    BN_free(bn);

    return {publicKey, privateKey};
}

std::string Encryption::encryptWithPublicKey(const std::string& publicKey, const std::string& data) {
    BIO *bio = BIO_new_mem_buf(publicKey.c_str(), -1);
    RSA *rsa = PEM_read_bio_RSAPublicKey(bio, NULL, NULL, NULL);
    BIO_free(bio);

    if (!rsa) {
        throw std::runtime_error("Failed to load RSA public key");
    }

    std::vector<unsigned char> encrypted(RSA_size(rsa));
    int encryptedLen = RSA_public_encrypt(data.size(), (const unsigned char*)data.c_str(), 
                                         encrypted.data(), rsa, RSA_PKCS1_PADDING);

    RSA_free(rsa);

    if (encryptedLen == -1) {
        throw std::runtime_error("RSA encryption failed");
    }

    return std::string((char*)encrypted.data(), encryptedLen);
}

std::string Encryption::decryptWithPrivateKey(const std::string& privateKey, const std::string& encryptedData) {
    BIO *bio = BIO_new_mem_buf(privateKey.c_str(), -1);
    RSA *rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
    BIO_free(bio);

    if (!rsa) {
        throw std::runtime_error("Failed to load RSA private key");
    }

    std::vector<unsigned char> decrypted(RSA_size(rsa));
    int decryptedLen = RSA_private_decrypt(encryptedData.size(), (const unsigned char*)encryptedData.c_str(),
                                          decrypted.data(), rsa, RSA_PKCS1_PADDING);

    RSA_free(rsa);

    if (decryptedLen == -1) {
        throw std::runtime_error("RSA decryption failed");
    }

    return std::string((char*)decrypted.data(), decryptedLen);
}

std::string Encryption::generateAESKey() {
    std::vector<unsigned char> key(32); // AES-256
    if (!RAND_bytes(key.data(), key.size())) {
        throw std::runtime_error("Failed to generate AES key");
    }
    return std::string((char*)key.data(), key.size());
}

std::string Encryption::generateIV() {
    std::vector<unsigned char> iv(16); // AES block size
    if (!RAND_bytes(iv.data(), iv.size())) {
        throw std::runtime_error("Failed to generate IV");
    }
    return std::string((char*)iv.data(), iv.size());
}

std::string Encryption::encryptAES(const std::string& key, const std::string& iv, const std::string& data) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, 
                      (const unsigned char*)key.c_str(), (const unsigned char*)iv.c_str());

    std::vector<unsigned char> ciphertext(data.size() + AES_BLOCK_SIZE);
    int len;

    EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                     (const unsigned char*)data.c_str(), data.size());
    int ciphertextLen = len;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertextLen += len;

    EVP_CIPHER_CTX_free(ctx);

    return std::string((char*)ciphertext.data(), ciphertextLen);
}

std::string Encryption::decryptAES(const std::string& key, const std::string& iv, const std::string& encryptedData) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, 
                      (const unsigned char*)key.c_str(), (const unsigned char*)iv.c_str());

    std::vector<unsigned char> plaintext(encryptedData.size());
    int len;

    EVP_DecryptUpdate(ctx, plaintext.data(), &len,
                     (const unsigned char*)encryptedData.c_str(), encryptedData.size());
    int plaintextLen = len;

    EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    plaintextLen += len;

    EVP_CIPHER_CTX_free(ctx);

    return std::string((char*)plaintext.data(), plaintextLen);
}

std::string Encryption::calculateSHA256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.size());
    SHA256_Final(hash, &sha256);

    std::string hashStr;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        hashStr += hex;
    }

    return hashStr;
}

bool Encryption::verifySHA256(const std::string& data, const std::string& hash) {
    std::string calculatedHash = calculateSHA256(data);
    return calculatedHash == hash;
}
```

#### 3.3.2 服务器消息处理修改
修改 `src/server/chatservice.cpp` 中的消息处理函数：

```cpp
// 修改oneChat函数
void ChatService::oneChat(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int toid = js["toid"].get<int>();

    // 处理加密消息
    if (js["msgId"].get<int>() == ENCRYPTED_MSG) {
        // 获取加密数据
        string encryptedKey = js["encryptedKey"];
        string iv = js["iv"];
        string encryptedContent = js["encryptedContent"];
        string checksum = js["checksum"];

        // 验证校验和
        if (!Encryption::verifySHA256(encryptedContent, checksum)) {
            LOG_ERROR << "Message checksum verification failed!";
            return;
        }

        // 使用服务器私钥解密AES密钥
        string aesKey = Encryption::decryptWithPrivateKey(_serverPrivateKey, encryptedKey);

        // 使用AES密钥解密消息内容
        string message = Encryption::decryptAES(aesKey, iv, encryptedContent);

        // 解析消息内容
        json msgjs = json::parse(message);

        // 获取解密后的消息内容
        int fromid = msgjs["id"].get<int>();
        string name = msgjs["name"];
        string msg = msgjs["msg"];
        string timeStr = msgjs["time"];

        // 构建新的JSON消息，用于转发
        json newJs;
        newJs["msgId"] = ONE_CHAT_MSG;
        newJs["id"] = fromid;
        newJs["name"] = name;
        newJs["msg"] = msg;
        newJs["time"] = timeStr;

        // 获取目标用户
        {
            lock_guard<mutex> lock(_connMutex);
            auto it = _userConnMap.find(toid);
            if (it != _userConnMap.end()) {
                // 目标用户在本服务器上在线，直接发送
                // 为目标用户生成新的AES密钥
                string newAESKey = Encryption::generateAESKey();
                string newIV = Encryption::generateIV();

                // 使用目标用户的公钥加密AES密钥
                string targetPublicKey = getUserPublicKey(toid); // 需要实现此函数
                string newEncryptedKey = Encryption::encryptWithPublicKey(targetPublicKey, newAESKey);

                // 使用新的AES密钥加密消息
                string newEncryptedContent = Encryption::encryptAES(newAESKey, newIV, newJs.dump());
                string newChecksum = Encryption::calculateSHA256(newEncryptedContent);

                // 构建加密消息
                json encryptedJs;
                encryptedJs["msgId"] = ENCRYPTED_MSG;
                encryptedJs["id"] = fromid;
                encryptedJs["name"] = name;
                encryptedJs["toid"] = toid;
                encryptedJs["encryptedKey"] = newEncryptedKey;
                encryptedJs["iv"] = newIV;
                encryptedJs["encryptedContent"] = newEncryptedContent;
                encryptedJs["time"] = timeStr;
                encryptedJs["checksum"] = newChecksum;

                it->second->send(encryptedJs.dump());
                return;
            }
        }

        // 目标用户不在线，存储离线消息
        // 存储加密消息
        _offlineMsgModel.insert(toid, js.dump());
        return;
    }

    // 处理明文消息（向后兼容）
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end()) {
            it->second->send(js.dump());
            return;
        }
    }

    User user = _userModel.query(toid);
    if (user.getState() == "online") {
        _redis.publish(toid, js.dump());
        return;
    }

    _offlineMsgModel.insert(toid, js.dump());
}
```

### 3.4 密钥管理

#### 3.4.1 密钥存储
创建 `src/server/keystore.cpp` 和 `src/server/keystore.hpp` 管理密钥：

```cpp
// keystore.hpp
#ifndef KEYSTORE_H
#define KEYSTORE_H

#include <string>
#include <unordered_map>

class KeyStore {
public:
    // 初始化密钥存储
    static void initialize();

    // 获取服务器私钥
    static std::string getServerPrivateKey();

    // 获取服务器公钥
    static std::string getServerPublicKey();

    // 获取用户公钥
    static std::string getUserPublicKey(int userId);

    // 设置用户公钥
    static void setUserPublicKey(int userId, const std::string& publicKey);

    // 保存密钥到文件
    static void saveToFile();

    // 从文件加载密钥
    static void loadFromFile();

private:
    static std::string _serverPrivateKey;
    static std::string _serverPublicKey;
    static std::unordered_map<int, std::string> _userPublicKeys;
};

#endif // KEYSTORE_H
```

#### 3.4.2 密钥交换
在用户登录时交换密钥：

```cpp
// 修改ChatService::login函数
void ChatService::login(const TcpConnectionPtr &con, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    // 获取用户公钥
    string userPublicKey = js["publicKey"]; // 客户端应在登录请求中包含公钥

    // 存储用户公钥
    KeyStore::setUserPublicKey(id, userPublicKey);

    // ... 其余登录逻辑保持不变 ...
}
```

## 4. 实施步骤

### 4.1 第一阶段：基础加密框架
1. 创建加密工具类（Encryption）
2. 实现基础加密/解密功能
3. 创建密钥管理类（KeyStore）

### 4.2 第二阶段：客户端加密
1. 修改客户端消息发送函数，添加加密逻辑
2. 修改客户端消息接收函数，添加解密逻辑
3. 在用户登录时交换密钥

### 4.3 第三阶段：服务器端加密
1. 修改服务器消息处理函数，添加解密/加密逻辑
2. 实现密钥管理功能
3. 处理密钥交换

### 4.4 第四阶段：测试与优化
1. 进行功能测试，确保加密/解密正确
2. 进行性能测试，评估加密对系统性能的影响
3. 优化加密算法和密钥管理策略

## 5. 安全注意事项

### 5.1 密钥安全
- 服务器私钥应存储在安全的位置，设置严格的访问权限
- 定期更换密钥，特别是AES会话密钥
- 实现密钥备份和恢复机制

### 5.2 通信安全
- 实现完美的前向保密（PFS）
- 添加消息序列号，防止重放攻击
- 实现会话超时机制，定期更换会话密钥

### 5.3 系统安全
- 及时更新OpenSSL库，修复已知漏洞
- 实现访问控制机制，防止未授权访问
- 添加日志记录，便于安全审计

## 6. 性能考虑

### 6.1 加密算法选择
- RSA-2048用于密钥交换，安全性高但计算开销大
- AES-256用于数据加密，速度快且安全性高
- SHA-256用于完整性校验

### 6.2 性能优化
- 对于大文件传输，考虑使用流式加密
- 实现加密缓存，减少重复计算
- 使用多线程处理加密/解密任务

## 7. 向后兼容性

### 7.1 渐进式加密
- 支持明文和加密消息共存
- 通过消息头中的"encrypted"字段区分
- 逐步淘汰明文消息

### 7.2 协议升级
- 实现协议版本控制
- 添加协商机制，确定使用加密还是明文
- 提供降级选项，兼容旧版本客户端

## 8. 测试计划

### 8.1 单元测试
- 加密/解密功能测试
- 密钥交换测试
- 完整性校验测试

### 8.2 集成测试
- 端到端消息传输测试
- 加密与明文混合通信测试
- 异常情况处理测试

### 8.3 安全测试
- 中间人攻击测试
- 重放攻击测试
- 密钥破解测试

## 9. 总结

本方案为聊天系统提供了全面的加密保护，通过混合加密机制确保了消息传输的安全性。方案采用渐进式实施策略，确保系统在升级过程中保持稳定运行。通过合理的密钥管理和安全措施，可以有效防止消息泄露、篡改等安全威胁，提高系统的整体安全性。

实施本方案后，聊天系统将具备以下安全特性：
1. 消息内容加密传输
2. 身份认证和密钥安全交换
3. 消息完整性校验
4. 防止重放攻击
5. 完美的前向保密

这些特性将大大提高聊天系统的安全性，保护用户隐私和数据安全。

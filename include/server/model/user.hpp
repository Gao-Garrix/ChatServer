#ifndef USER_H
#define USER_H

#include <string>
using namespace std;

// User表的ORM类
class User
{
protected:
    int id;
    string name;
    string password;
    string state;
public:
    // 构造方法初始化对象
    User(int id=-1, string name="", string pwd="", string state="offline")
    {
        this->id = id;
        this->name = name;
        this->password = pwd;
        this->state = state;
    }

    // 设置接口修改成员变量
    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setPwd(string pwd) { this->password = pwd; }
    void setState(string state) { this->state = state; }

    // 设置接口获取成员变量
    int getId() { return this->id; }
    string getName() { return this->name; }
    string getPwd() { return this->password; }
    string getState() { return this->state; }
};

#endif
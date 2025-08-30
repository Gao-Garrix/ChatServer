#ifndef GROUP_H
#define GROUP_H

#include "groupuser.hpp"
#include <string>
#include <vector>
using namespace std;

// Group表的ORM类
class Group
{
public:
    // 构造方法初始化对象
    Group(int id = -1, string name = "", string desc = "")
    {
        this->id = id;
        this->name = name;
        this->desc = desc;
    }

    // 设置接口修改成员变量
    void setId(int id) { this->id = id; }
    void setName(string name) { this->name = name; }
    void setDesc(string desc) { this->desc = desc; }

    // 设置接口获取成员变量
    int getId() { return this->id; }
    string getName() { return this->name; }
    string getDesc() { return this->desc; }
    vector<GroupUser> &getUsers() { return this->users; }

private:
    int id;       // 组id
    string name;  // 组名
    string desc;  // 组描述
    vector<GroupUser> users; // 存储从数据库查到的组id的所有成员
};

#endif
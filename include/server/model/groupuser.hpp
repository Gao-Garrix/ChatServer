#ifndef GROUPUSER_H
#define GROUPUSER_H

#include "user.hpp"

// 群组用户类,添加新的成员变量role,从User类直接继承,复用User的其它信息
class GroupUser : public User
{
public:
    // 修改用户角色信息
    void setRole(string role) { this->role = role; }
    
    // 获取用户角色信息
    string getRole() { return this->role; }

private:
    string role; // 用户的角色信息
};

#endif
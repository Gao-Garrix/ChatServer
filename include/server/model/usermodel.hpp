#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

// User表的数据操作类
class UserModel
{
public:
    // User表添加新用户的方法 参数为User对象的引用 
    bool insert(User &user);

    // 根据用户id查询用户信息
    User query(int id);

    // 更新用户的状态信息
    bool updateState(User user);

    // 重置用户的状态信息
    void resetState();
};

#endif
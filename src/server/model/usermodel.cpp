#include "usermodel.hpp"
#include "db.h"
#include <iostream>
using namespace std;

// User表添加新用户的方法 参数为User对象的引用
bool UserModel::insert(User &user)
{
    // 组装插入语句,并存入sql字符数组
    char sql[1024] = {0};

    // id是自增的,不需要插入
    sprintf(sql, "insert into user(name, password, state) values('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        if (mysql.update(sql)) // 将插入语句传给数据库更新函数update,若数据库更新成功
        {
            // mysql_insert_id函数: 获取插入成功的用户数据生成的主键id
            user.setId(mysql_insert_id(mysql.getConnection()));
            return true;
        }
    }
    return false;
}

// 根据用户id查询用户信息
User UserModel::query(int id)
{
    // 组装查询语句,并存入sql字符数组
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        // 将查询语句传给数据库的查询函数query,返回MYSQL_RES型指针res
        // 指针 res 指向的结果集 MYSQL_RES 是在 MySQL C API 内部动态分配的内存，用于存储查询结果
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr) // 查询成功
        {
            // mysql_fetch_row: 获取结果集的一行 (这里的结果集只有一行,因为id是主键,是唯一的)
            // MYSQL_ROW 是一个指向字符串数组的指针,数组的每个元素对应user表的一个字段的字符串类型数据
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) // 行不为空,即行中有数据
            {
                User user;

                // row[0] 存第一个字段id的数据
                user.setId(atoi(row[0])); // 将id字段数据类型从字符串转为整数

                // row[1] 存第二个字段name的数据
                user.setName(row[1]);

                // row[2] 存第三个字段password的数据
                user.setPwd(row[2]);

                // row[3] 存第三个字段state的数据
                user.setState(row[3]);

                // 释放指针res的资源,否则内存不断泄露,
                // 因为每次调用 mysql.query() 都会分配新的内存给新结果集，而旧结果集的内存没有被正确地释放。
                mysql_free_result(res);
                return user;
            }
        }
    }

    // 没有找到该用户id的数据
    return User(); // 构造函数User()创建一个id=-1的错误用户并作为返回值
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    // 组装状态更新语句,并存入sql字符数组
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        if (mysql.update(sql)) // 将状态更新语句传给数据库更新函数update,若数据库更新成功
        {
            return true;
        }
    }
    return false;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    // 组装状态更新语句,并存入sql字符数组
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        mysql.update(sql); // 将状态更新语句传给数据库更新函数update,若数据库更新成功
    }
}
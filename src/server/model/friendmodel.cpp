#include "friendmodel.hpp"
#include "db.h"

// 添加好友关系
void FriendModel::insert(int userid, int friendid)
{
    // 组装插入语句,并存入sql字符数组
    char sql[1024] = {0};

    sprintf(sql, "insert into friend values(%d, %d)", userid, friendid);

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        mysql.update(sql); // 将插入语句传给数据库更新函数update
    }
}

// 返回用户好友列表
vector<User> FriendModel::query(int userid)
{
    // 组装查询语句,并存入sql字符数组
    char sql[1024] = {0};
    sprintf(sql, "select user.id, user.name, user.state from user inner join friend on friend.friendid = user.id where friend.userid = %d", userid);

    vector<User> vec; // 存储查询到的该用户userid的所有好友
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
            MYSQL_ROW row;
            
            // 把该用户的所有好友放入好友列表vec中,然后返回vec
            while((row = mysql_fetch_row(res)) != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));  // row[0]存储查到的好友id的字符串型数据,用函数atoi将其转成int型
                user.setName(row[1]);      // row[1]存储查到的好友的名字
                user.setState(row[2]);     // row[2]存储查到的好友的状态
                vec.push_back(user);       // 将该好友添加到好友列表中
            }
            mysql_free_result(res); // 释放资源
            return vec;
        }
    }
    return vec;
}
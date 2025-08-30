#include "offlinemessagemodel.hpp"
#include "db.h"

// 存储用户的离线消息
void offlineMsgModel::insert(int userid, string msg)
{
    // 组装插入语句,并存入sql字符数组
    char sql[1024] = {0};

    sprintf(sql, "insert into offlinemessage values(%d, '%s')", userid, msg.c_str());

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        mysql.update(sql); // 将插入语句传给数据库更新函数update,若数据库更新成功
    }
}

// 删除用户的离线消息
void offlineMsgModel::remove(int userid)
{
    // 组装插入语句,并存入sql字符数组
    char sql[1024] = {0};

    sprintf(sql, "delete from offlinemessage where userid = %d", userid);

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        mysql.update(sql); // 将插入语句传给数据库更新函数update,若数据库更新成功
    }
}

// 查询用户的离线消息,可能有多条,查到的消息用vector容器存储
vector<string> offlineMsgModel::query(int userid)
{
    // 组装查询语句,并存入sql字符数组
    char sql[1024] = {0};
    sprintf(sql, "select message from offlinemessage where userid = %d", userid);

    vector<string> vec; // 存储查询到的所有离线消息
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
            
            // 把该用户的所有离线消息放入vec中,然后返回vec
            while((row = mysql_fetch_row(res)) != nullptr)
            {
                vec.push_back(row[0]); // 只查询了message字段,故row[0]存的就是离线消息
            }
            mysql_free_result(res); // 释放资源
            return vec;
        }
    }
    return vec;
}
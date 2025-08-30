#include "groupmodel.hpp"
#include "db.h"

// 创建群组
bool GroupModel::createGroup(Group &group)
{
    // 组装插入语句,并存入sql字符数组
    char sql[1024] = {0};

    // 群组id是自动生成的,不需要插入
    sprintf(sql, "insert into allgroup(groupname, groupdesc) values('%s', '%s')",
            group.getName().c_str(), group.getDesc().c_str());

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        if (mysql.update(sql)) // 将插入语句传给数据库更新函数update,若数据库更新成功
        {
            // mysql_insert_id(mysql.getConnection()): 获取本次数据库连接执行insert语句生成的自增的群组id值
            group.setId(mysql_insert_id(mysql.getConnection())); // 设置新群组的群组id
            return true;
        }
    }
    return false;
}


// 加入群组
void GroupModel::addGroup(int userid, int groupid, string role)
{
    // 组装sql语句,并存入sql字符数组
    char sql[1024] = {0};

    sprintf(sql, "insert into groupuser values(%d, %d, '%s')",
            groupid, userid, role.c_str());

    MySQL mysql;
    if (mysql.connect()) // 数据库连接成功
    {
        mysql.update(sql); // 将插入语句传给数据库更新函数update,更新数据库
    }
}


// 查询用户的所有群组信息
vector<Group> GroupModel::queryGroups(int userid)
{
    /*
    1. 先根据userid在groupuser表中查询出该用户所属的群组信息
    2. 再根据群组信息，查询属于该群组的所有用户的userid，并且和user表进行多表联合查询，查出用户的详细信息
    */

    // 组装sql语句,并存入sql字符数组
    char sql[1024] = {0};

    sprintf(sql, "select a.id, a.groupname, a.groupdesc from allgroup a \
            inner join groupuser b on a.id = b.groupid where b.userid = %d",
            userid);

    vector<Group> groupVec; // 存储用户userid的所有群组和各群组中所有组员用户的信息

    MySQL mysql;
    if (mysql.connect())
    {
        // 将查询语句传给数据库的查询函数query,返回MYSQL_RES型指针res
        // 指针 res 指向的结果集 MYSQL_RES 是在 MySQL C API 内部动态分配的内存，用于存储查询结果
        MYSQL_RES *res = mysql.query(sql);
        
        if (res != nullptr) // 查询成功
        {
            // mysql_fetch_row: 获取结果集的一行 (这里的结果集只有一行,因为id是主键,是唯一的)
            // MYSQL_ROW 是一个指向字符串数组的指针,数组的每个元素对应user表的一个字段的字符串类型数据
            MYSQL_ROW row;

            // 查询该用户userid所有群组的信息,并把该用户的所有群组放入群组列表groupVec中
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                Group group;
                group.setId(atoi(row[0])); // row[0]存的是组id
                group.setName(row[1]);     // row[1]存的是组名
                group.setDesc(row[2]);     // row[2]存的是组描述
                groupVec.push_back(group); // 将该群组添加到群组列表groupVec中
            }
            mysql_free_result(res); // 释放资源
        }
    }

    // 查询该用户userid所有群组中的成员用户信息
    for (Group &group : groupVec)
    {
        sprintf(sql, "select a.id, a.name, a.state, b.grouprole from user a \
                inner join groupuser b on b.userid = a.id where b.groupid = %d",
                group.getId());

        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                GroupUser user;
                user.setId(atoi(row[0]));         // row[0]存的是用户id
                user.setName(row[1]);             // row[1]存的是用户名
                user.setState(row[2]);            // row[2]存的是用户状态
                user.setRole(row[3]);             // row[3]存的是用户在群组中的角色
                group.getUsers().push_back(user); // 将该用户添加到该群组的成员列表中
            }
            mysql_free_result(res); // 释放资源
        }
    }
    return groupVec; // 返回该用户userid的群组列表
}


// 查询群组groupid中除了用户userid之外的其他组员的用户id,主要用户群聊业务给群组其它成员群发消息
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    // 组装sql语句,并存入sql字符数组
    char sql[1024] = {0};

    sprintf(sql, "select userid from groupuser where groupid = %d and userid != %d", groupid, userid);

    vector<int> idVec; // 存储群组groupid中除了用户userid之外的其他组员用户的id
    
    MySQL mysql;
    if (mysql.connect()) // 连接成功
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr) // 查询成功
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                idVec.push_back(atoi(row[0])); // 将该用户的id存到id列表中
            }
            mysql_free_result(res); // 释放资源
        }
    }
    return idVec; // 返回群组中除userid之外的其他组员的id列表
}
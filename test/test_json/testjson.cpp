#include "../include/json.hpp"
using json = nlohmann::json;

#include<iostream>
#include<vector>
#include<map>
#include<string>
using namespace std;

// json序列化示例1-普通数据序列化
// void func1(){
//     json js;
//     // 添加数组
//     js["id"] = {1,2,3,4,5};
//     // 添加key-value
//     js["name"] = "zhang san";
//     // 添加对象
//     js["msg"]["zhang san"] = "hello world";
//     js["msg"]["liu shuo"] = "hello china";
//     string sendbuf = js.dump();
//     cout << sendbuf.c_str() << endl;
// }

// json反序列化
string func1(){
    json js;
    // 添加数组
    js["id"] = {1,2,3,4,5};
    // 添加key-value
    js["name"] = "zhang san";
    // 添加对象
    js["msg"]["zhang san"] = "hello world";
    js["msg"]["liu shuo"] = "hello china";
    string sendbuf = js.dump(); // json数据对象 -> 序列化json字符串
    return sendbuf;
    // cout << sendbuf.c_str() << endl;
}

// json序列化示例2-容器序列化
// void func2(){
//     json js;
//     // 直接序列化一个vector容器
//     vector<int> vec;
//     vec.push_back(1);
//     vec.push_back(2);
//     vec.push_back(5);
//     js["list"] = vec;
//     // 直接序列化一个map容器
//     map<int, string> m;
//     m.insert({1, "黄山"});
//     m.insert({2, "华山"});
//     m.insert({3, "泰山"});
//     js["path"] = m;
//     string sendbuf = js.dump();
//     cout << sendbuf.c_str() << endl;
//     cout<< sendbuf <<endl;
// }

string func2(){
    json js;
    // 直接序列化一个vector容器
    vector<int> vec;
    vec.push_back(1);
    vec.push_back(2);
    vec.push_back(5);
    js["list"] = vec;
    // 直接序列化一个map容器
    map<int, string> m;
    m.insert({1, "黄山"});
    m.insert({2, "华山"});
    m.insert({3, "泰山"});
    js["path"] = m;
    string sendbuf = js.dump();
    return sendbuf;
    // cout << sendbuf.c_str() << endl;
    // cout<< sendbuf <<endl;
}

int main(){
    // func1();
    // func2();

    // 数据的反序列化 json字符串 -> 反序列化数据对象（看作容器，方便访问）
    // string recvbuf = func1();
    // json jsbuf = json::parse(recvbuf);
    // cout << jsbuf["id"] << endl;
    // auto arr = jsbuf["id"];
    // cout << arr[2] << endl;
    // cout << jsbuf["msg"] << endl;
    // cout << jsbuf["name"] << endl;
    
    string recvbuf = func2();
    json jsbuf = json::parse(recvbuf);
    vector<int> vec = jsbuf["list"];
    for(auto& v: vec){
        cout << v << " ";
    }
    cout << endl;

    map<int, string> mmap = jsbuf["path"];
    for(auto& m: mmap){
        cout << m.first << " " << m.second << endl;
    }
    cout << endl;
    return 0;
}

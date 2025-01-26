# ChatServer
这是一个可以工作在nginx tcp负载均衡环境中、基于陈硕大佬的muduo库实现的集群聊天服务器和客户端源码。使用了基于发布-订阅的redis消息队列作为负责跨服务器之间通信的中间件，使用mysql数据库存储用户数据。 

编译方式：
cd build
rm -rf *
cmake ..
make

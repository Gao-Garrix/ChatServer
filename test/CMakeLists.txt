cmake_minimum_required(VERSION 3.0)
project(main)

# g++ -o server -g muduo_server.cpp xxx.cpp -I/usr/include -L/usr/lib -lmuduo_net -lmuduo_base -lpthread
# part   1      2  3                        4              5          6

# 配置编译选项 (-o、-g等) (part3)
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -g)

# 配置头文件搜索路径 (part4)
# include_directories()

# 配置库文件搜索路径 (part5)
# link_directories()

# 设置需要编译的源文件列表
set(SRC_LIST ./muduo_server.cpp)

# 设置可执行文件存放路径
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 把.指定路径下的所有源文件名字放入变量SRC_LIST里面
# aux_source_directory(. SRC_LIST)

# 可执行文件 server 由 SRC_LIST 变量所定义的源文件编译而来 (part1)
add_executable(server ${SRC_LIST})

# server 这个目标程序需要链接 muduo_net muduo_base pthread 这三个库文件 (part6)
target_link_libraries(server muduo_net muduo_base pthread)
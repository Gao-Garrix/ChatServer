# 定义SRC_LIST变量,包含了client目录下的所有的源文件
aux_source_directory(. SRC_LIST)

# 指定生成可执行文件ChatClient
add_executable(ChatClient ${SRC_LIST})

# 指定可执行文件链接时需要依赖的库文件
target_link_libraries(ChatClient pthread)
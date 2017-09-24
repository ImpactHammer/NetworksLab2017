cmake_minimum_required(VERSION 2.8)
project(server_lin_mt)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set(SOURCE_FILES main.c)
add_executable(server_lin_mt ${SOURCE_FILES})
target_link_libraries(server_lin_mt pthread)
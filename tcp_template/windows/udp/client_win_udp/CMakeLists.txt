cmake_minimum_required(VERSION 2.8)
project(client_win_udp)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set(SOURCE_FILES main.c)
add_executable(client_win_udp ${SOURCE_FILES})
target_link_libraries(client_win_udp wsock32)
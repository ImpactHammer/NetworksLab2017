cmake_minimum_required(VERSION 2.8)
project(client_windows)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")

set(SOURCE_FILES main.c)
add_executable(client_windows ${SOURCE_FILES})
target_link_libraries(client_windows wsock32)
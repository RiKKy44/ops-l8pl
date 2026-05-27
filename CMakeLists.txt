cmake_minimum_required(VERSION 3.10)
project(WaterlooServer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pthread")

add_executable(server sop-mag.c l8_common.h)

target_link_libraries(server PRIVATE pthread)
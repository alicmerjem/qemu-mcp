cmake_minimum_required(VERSION 3.16)
project(qemu-mcp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

add_executable(qemu-mcp
    src/main.cpp
    src/mcp_server.cpp
)

target_include_directories(qemu-mcp PRIVATE
    include
    third_party
)
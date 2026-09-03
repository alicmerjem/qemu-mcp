cmake_minimum_required(VERSION 3.16)
project(qemu-mcp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

add_executable(qemu-mcp
    src/main.cpp
    src/mcp_server.cpp
    src/qemu_instance.cpp
    src/qmp_client.cpp
    src/gdb_client.cpp
    src/event_log.cpp
)

target_link_libraries(qemu-mcp PRIVATE Threads::Threads)

target_include_directories(qemu-mcp PRIVATE
    include
    third_party
)
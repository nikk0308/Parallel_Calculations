cmake_minimum_required(VERSION 3.28)
project(Server)

set(CMAKE_CXX_STANDARD 17)

add_executable(Server main.cpp)

set(WS2_32_LIBRARY ws2_32)
target_link_libraries(Server PRIVATE ${WS2_32_LIBRARY})

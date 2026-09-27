cmake_minimum_required(VERSION 3.16)
project(UdpGame CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Общий протокол
add_library(proto STATIC src/common/protocol.cpp)
target_include_directories(proto PUBLIC src/common)

add_executable(udp_server src/server/server.cpp)
target_link_libraries(udp_server PRIVATE proto)
if (WIN32)
  target_link_libraries(udp_server PRIVATE ws2_32)
  target_link_options(udp_server PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

add_executable(udp_client src/client/client.cpp)
target_link_libraries(udp_client PRIVATE proto)
if (WIN32)
  target_link_libraries(udp_client PRIVATE ws2_32)
  target_link_options(udp_client PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

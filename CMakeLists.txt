cmake_minimum_required (VERSION 3.10)
project (q2unpack)

find_package(PNG)
set (CMAKE_CXX_STANDARD 11)

add_executable(q2unpack src/main.cpp
    src/files.h)

target_include_directories(q2unpack PUBLIC ${PNG_INCLUDE_DIRS})
target_link_libraries (q2unpack ${PNG_LIBRARIES})

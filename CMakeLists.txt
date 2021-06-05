cmake_minimum_required(VERSION 3.5)

project(compact_bitset LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    # warning level 3
    add_compile_options(/W3)
else()
    # lots of warnings
    add_compile_options(-Wall -pedantic)
endif()

add_executable(compact_bitset main.cpp)

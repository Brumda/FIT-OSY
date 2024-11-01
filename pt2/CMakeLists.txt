cmake_minimum_required(VERSION 3.22)
project(pt2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pedantic -Wall -Wextra -Wno-long-long")
add_executable(pt2
        solution.cpp
        tests.inc
        tests2.inc)

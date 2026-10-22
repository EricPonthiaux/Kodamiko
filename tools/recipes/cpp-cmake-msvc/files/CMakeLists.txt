cmake_minimum_required(VERSION 3.24)

project({{identifier}} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable({{identifier}} src/main.cpp)

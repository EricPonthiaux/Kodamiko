cmake_minimum_required(VERSION 3.24)

project({{identifier}} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable({{identifier}}
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
)
target_link_libraries({{identifier}} PRIVATE Qt6::Widgets)
# A window without a console on Windows.
set_target_properties({{identifier}} PROPERTIES WIN32_EXECUTABLE ON)

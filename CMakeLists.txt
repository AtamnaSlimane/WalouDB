cmake_minimum_required(VERSION 3.20)

project(WalouDB VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(WalouDB
    src/main.cpp
    src/core/Database.cpp
)
target_include_directories(WalouDB
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(WalouDB PRIVATE src)

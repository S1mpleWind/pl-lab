cmake_minimum_required(VERSION 3.16)
project(compilerlab LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(lab_ast src/ast/ast.cpp src/ast/json.cpp)
target_include_directories(lab_ast PUBLIC src)
target_compile_options(lab_ast PRIVATE -Wall -Wextra -Wpedantic)


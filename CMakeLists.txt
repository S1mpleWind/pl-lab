cmake_minimum_required(VERSION 3.16)
project(compilerlab LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(lab_ast src/ast/ast.cpp src/ast/json.cpp)
target_include_directories(lab_ast PUBLIC src)
target_compile_options(lab_ast PRIVATE -Wall -Wextra -Wpedantic)

find_package(BISON 3.8 REQUIRED)
find_package(FLEX 2.6 REQUIRED)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/frontend")
BISON_TARGET(LabParser src/frontend/parser.y
    "${CMAKE_CURRENT_BINARY_DIR}/generated/frontend/parser.cpp"
    DEFINES_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/frontend/parser.hpp"
    COMPILE_FLAGS "-Wall -Werror=conflicts-sr -Werror=conflicts-rr --report=state")
FLEX_TARGET(LabLexer src/frontend/lexer.l
    "${CMAKE_CURRENT_BINARY_DIR}/generated/frontend/scanner.cpp")
ADD_FLEX_BISON_DEPENDENCY(LabLexer LabParser)
add_library(lab_frontend src/frontend/lexer.cpp src/frontend/parse.cpp
    ${BISON_LabParser_OUTPUTS} ${FLEX_LabLexer_OUTPUTS})
target_include_directories(lab_frontend PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(lab_frontend PUBLIC lab_ast)
target_compile_options(lab_frontend PRIVATE -Wall -Wextra -Wpedantic)
add_executable(labc src/driver/main.cpp)
target_link_libraries(labc PRIVATE lab_frontend)
target_compile_options(labc PRIVATE -Wall -Wextra -Wpedantic)


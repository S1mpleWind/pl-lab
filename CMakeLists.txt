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
# 生成文件只进入 build；规则文件仍是前端的唯一手工维护入口。
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
find_package(Threads REQUIRED)
# runtime 自带程序入口，链接到生成的 ELF；labc 不链接这个 main。
add_library(lab_runtime STATIC runtime/runtime.cpp)
target_link_libraries(lab_runtime PUBLIC Threads::Threads)
target_compile_options(lab_runtime PRIVATE -Wall -Wextra -Wpedantic)
add_library(lab_middle src/sema/check.cpp src/ir/emit.cpp src/backend/build.cpp)
target_link_libraries(lab_middle PUBLIC lab_ast)
target_compile_options(lab_middle PRIVATE -Wall -Wextra -Wpedantic)
add_executable(labc src/driver/main.cpp)
target_link_libraries(labc PRIVATE lab_frontend lab_middle)
# 构建 labc 时也准备好运行时 archive，供后端后续链接使用。
add_dependencies(labc lab_runtime)
target_compile_options(labc PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_package(Threads REQUIRED)
    add_executable(frontend_api_test tests/frontend_api.cpp)
    target_link_libraries(frontend_api_test PRIVATE lab_frontend Threads::Threads)
    target_compile_options(frontend_api_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME frontend_api COMMAND frontend_api_test)
    add_test(NAME frontend_cli COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/frontend_test.py"
        --compiler $<TARGET_FILE:labc>)
    add_test(NAME native_compiler COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/compiler_test.py"
        --compiler $<TARGET_FILE:labc>)
    set_tests_properties(native_compiler PROPERTIES TIMEOUT 240)
    set_tests_properties(frontend_api frontend_cli PROPERTIES TIMEOUT 60)
endif()

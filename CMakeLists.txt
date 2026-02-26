cmake_minimum_required(VERSION 3.10)
project(OCL C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── Compiler flags ─────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4 /wd4996)
else()
    add_compile_options(-Wall -Wextra -pedantic -fPIC)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O2)
    endif()
endif()

# ── Include dirs ────────────────────────────────────────────
include_directories(${CMAKE_SOURCE_DIR}/include)

# ── Core library sources ─────────────────────────────────────
add_library(ocl_core STATIC
    src/common.c
    src/interpreter/lexer.c
    src/interpreter/ast.c
    src/interpreter/parser.c
    src/interpreter/type_checker.c
    src/interpreter/codegen.c
    src/vm/bytecode.c
    src/vm/vm.c
    src/vm/runtime.c
    src/stdlib/stdlib.c
    src/frontend/errors.c
)

# ── Main executable ──────────────────────────────────────────
add_executable(ocl src/frontend/main.c)
target_link_libraries(ocl ocl_core)

if(UNIX)
    target_link_libraries(ocl m)
endif()

# ── Tests ────────────────────────────────────────────────────
enable_testing()

function(add_ocl_test name src)
    add_executable(${name} ${src})
    target_link_libraries(${name} ocl_core)
    if(UNIX)
        target_link_libraries(${name} m)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_ocl_test(test_lexer       test/test_lexer.c)
add_ocl_test(test_parser      test/test_parser.c)
add_ocl_test(test_type_checker test/test_type_checker.c)
add_ocl_test(test_vm          test/test_vm.c)

# ── Info ──────────────────────────────────────────────────────
message(STATUS "OCL Interpreter v0.2.0")
message(STATUS "  System:    ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:  ${CMAKE_C_COMPILER}")
message(STATUS "  C Std:     ${CMAKE_C_STANDARD}")
message(STATUS "  Build:     ${CMAKE_BUILD_TYPE}")
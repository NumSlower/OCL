cmake_minimum_required(VERSION 3.16)

project(OCL LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

file(GLOB_RECURSE OCL_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.c"
)
list(FILTER OCL_SOURCES EXCLUDE REGEX ".*/src/vm/vm_legacy\\.c$")

find_program(OCL_RUSTC rustc REQUIRED)
set(OCL_RUST_VM_LIB "${CMAKE_BINARY_DIR}/vm_rust.a")

add_custom_command(
    OUTPUT "${OCL_RUST_VM_LIB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}"
    COMMAND
        "${OCL_RUSTC}"
        --crate-name ocl_vm_rust
        --crate-type staticlib
        --edition=2021
        -C panic=abort
        -o "${OCL_RUST_VM_LIB}"
        "${PROJECT_SOURCE_DIR}/src/vm/vm.rs"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/src/vm/vm.rs"
        "${PROJECT_SOURCE_DIR}/src/vm/vm.c"
        "${PROJECT_SOURCE_DIR}/include/bytecode.h"
        "${PROJECT_SOURCE_DIR}/include/common.h"
        "${PROJECT_SOURCE_DIR}/include/errors.h"
        "${PROJECT_SOURCE_DIR}/include/ocl_stdlib.h"
        "${PROJECT_SOURCE_DIR}/include/vm.h"
    VERBATIM
)

add_custom_target(ocl_vm_rust ALL DEPENDS "${OCL_RUST_VM_LIB}")

add_executable(ocl ${OCL_SOURCES})
add_dependencies(ocl ocl_vm_rust)

target_compile_definitions(ocl PRIVATE _POSIX_C_SOURCE=200809L)

if(MSVC)
    target_include_directories(ocl PRIVATE
        "${PROJECT_SOURCE_DIR}/include"
    )
    target_compile_options(ocl PRIVATE /W4 /wd4100)
else()
    target_compile_options(ocl PRIVATE
        -iquote
        "${PROJECT_SOURCE_DIR}/include"
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
    )
endif()

if(WIN32)
    target_link_libraries(ocl PRIVATE "${OCL_RUST_VM_LIB}" ws2_32 bcrypt)
else()
    target_link_libraries(ocl PRIVATE "${OCL_RUST_VM_LIB}" m)
endif()

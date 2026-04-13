cmake_minimum_required(VERSION 3.16)

project(OCL LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

file(GLOB_RECURSE OCL_SOURCES CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/*.c"
)

add_executable(ocl ${OCL_SOURCES})

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
    target_link_libraries(ocl PRIVATE ws2_32 bcrypt)
endif()

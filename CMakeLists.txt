cmake_minimum_required(VERSION 3.10)
project(OCL C)

# Set C standard to C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Platform-specific settings
if(WIN32)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")  # MSVC warnings
elseif(APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic -fPIC")
else()  # Linux and other Unix-like systems
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -pedantic -fPIC")
    # Enable address sanitizer for debugging (optional)
    # set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files for interpreter and VM
set(INTERPRETER_SOURCES
    src/interpreter/lexer.c
    src/interpreter/ast.c
    src/interpreter/parser.c
    src/interpreter/type_checker.c
    src/interpreter/codegen.c
)

set(VM_SOURCES
    src/vm/bytecode.c
    src/vm/vm.c
    src/vm/runtime.c
)

set(STDLIB_SOURCES
    src/stdlib/stdlib.c
)

set(FRONTEND_SOURCES
    src/frontend/main.c
    src/frontend/errors.c
)

# Create static library for interpreter components
add_library(ocl_interpreter STATIC
    src/common.c
    ${INTERPRETER_SOURCES}
    ${VM_SOURCES}
    ${STDLIB_SOURCES}
)

# Create main executable
add_executable(ocl
    ${FRONTEND_SOURCES}
)

target_link_libraries(ocl ocl_interpreter)

# Platform-specific linking
if(UNIX AND NOT APPLE)
    target_link_libraries(ocl m)  # Link math library on Linux
elseif(APPLE)
    target_link_libraries(ocl m)  # Link math library on macOS
endif()

# Enable testing
enable_testing()

# TODO: Implement proper test framework later

# Optional: Print build info
message(STATUS "OCL Interpreter Build Configuration")
message(STATUS "  System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")

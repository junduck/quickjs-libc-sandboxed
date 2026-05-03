cmake_minimum_required(VERSION 3.20)
project(quickjs-libc-sandboxed VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(sandboxed-fs)

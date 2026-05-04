cmake_minimum_required(VERSION 3.20)
project(quickjs-libc-sandboxed VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(BUILD_TESTS "Build tests" ON)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

find_package(Boost CONFIG REQUIRED)

# --- QuickJS (submodule) ---

file(READ "${CMAKE_SOURCE_DIR}/quickjs/VERSION" QUICKJS_VERSION)
string(STRIP "${QUICKJS_VERSION}" QUICKJS_VERSION)
string(REGEX MATCH "^[0-9]+-[0-9]+-[0-9]+" QUICKJS_VERSION "${QUICKJS_VERSION}")

set(QUICKJS_INCLUDE "${CMAKE_BINARY_DIR}/quickjs_include")
file(MAKE_DIRECTORY "${QUICKJS_INCLUDE}")
file(COPY "${CMAKE_SOURCE_DIR}/quickjs/quickjs.h" DESTINATION "${QUICKJS_INCLUDE}")
file(COPY "${CMAKE_SOURCE_DIR}/quickjs/quickjs-libc.h" DESTINATION "${QUICKJS_INCLUDE}")

add_library(quickjs STATIC
    quickjs/quickjs.c
    quickjs/dtoa.c
    quickjs/libregexp.c
    quickjs/libunicode.c
    quickjs/cutils.c
)

target_include_directories(quickjs
    PRIVATE "${CMAKE_SOURCE_DIR}/quickjs"
    PUBLIC "${QUICKJS_INCLUDE}"
)

target_compile_definitions(quickjs PRIVATE
    _GNU_SOURCE
    "CONFIG_VERSION=\"${QUICKJS_VERSION}\""
)

if(APPLE)
    target_compile_definitions(quickjs PRIVATE CONFIG_DARWIN)
endif()

target_compile_options(quickjs PRIVATE
    -fwrapv
    -funsigned-char
    -Wall
    -Wextra
    -Wno-sign-compare
    -Wno-missing-field-initializers
    -Wundef
    -Wuninitialized
    -Wunused
    -Wno-unused-parameter
    -Wwrite-strings
    -Wchar-subscripts
    -Wno-unterminated-string-initialization
)

# --- Sandboxed modules ---

add_subdirectory(sandboxed-fs)

# --- Core runtime ---

add_library(sandboxed-core STATIC
    src/runtime.cpp
    src/context.cpp
    src/promise.cpp
    src/primordials.cpp
    src/bindings/timers.cpp
    src/bindings/print.cpp
)

target_include_directories(sandboxed-core PUBLIC include)
target_link_libraries(sandboxed-core PUBLIC Boost::headers quickjs sandboxed-fs)
target_compile_features(sandboxed-core PUBLIC cxx_std_23)

# --- Demo ---

add_executable(sandboxed-demo demo/main.cpp)
target_link_libraries(sandboxed-demo PRIVATE sandboxed-core)

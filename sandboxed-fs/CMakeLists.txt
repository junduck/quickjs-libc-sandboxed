add_library(sandboxed-fs STATIC
    src/vfs.cpp
    src/vfs_real.cpp
    src/vfs_mem.cpp
)

target_include_directories(sandboxed-fs PUBLIC include)
target_compile_features(sandboxed-fs PUBLIC cxx_std_23)

if(BUILD_TESTS)
    include(GoogleBenchAndTest)
    add_subdirectory(tests)
endif()

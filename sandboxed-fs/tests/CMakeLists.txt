add_executable(test_vfs test_vfs.cpp)

target_link_libraries(test_vfs PRIVATE sandboxed-fs gtest_main gmock)

add_test(NAME test_vfs COMMAND test_vfs)

add_executable(bench_vfs bench_vfs.cpp)

target_link_libraries(bench_vfs PRIVATE sandboxed-fs benchmark::benchmark)

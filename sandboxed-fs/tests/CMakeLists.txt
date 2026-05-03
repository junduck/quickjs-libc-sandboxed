add_executable(test_vfs test_vfs.cpp)

target_link_libraries(test_vfs PRIVATE sandboxed-fs gtest_main gmock)

add_test(NAME test_vfs COMMAND test_vfs)

add_executable(test_security test_security.cpp)

target_link_libraries(test_security PRIVATE sandboxed-fs gtest_main gmock)

add_test(NAME test_security COMMAND test_security)

add_executable(demo_escape demo_escape.cpp)

target_link_libraries(demo_escape PRIVATE sandboxed-fs)

add_executable(demo_bug demo_bug.cpp)

target_link_libraries(demo_bug PRIVATE sandboxed-fs)

add_executable(bench_vfs bench_vfs.cpp)

target_link_libraries(bench_vfs PRIVATE sandboxed-fs benchmark::benchmark)

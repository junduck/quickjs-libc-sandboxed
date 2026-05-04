add_executable(test_opaque test_opaque.cpp)
target_link_libraries(test_opaque PRIVATE sandboxed-core gtest_main)
add_test(NAME test_opaque COMMAND test_opaque)

add_executable(test_value test_value.cpp)
target_link_libraries(test_value PRIVATE sandboxed-core gtest_main)
add_test(NAME test_value COMMAND test_value)

add_executable(test_call_minimal test_call_minimal.cpp)
target_link_libraries(test_call_minimal PRIVATE sandboxed-core gtest_main)
add_test(NAME test_call_minimal COMMAND test_call_minimal)

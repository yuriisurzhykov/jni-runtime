jnirt_add_test(support_logging_test logging_test.cpp)
jnirt_add_test(support_error_test error_test.cpp)

# The same assert tests in both build flavours: JNIRT_ASSERT depends on NDEBUG of the caller's translation unit.
jnirt_add_test(support_assert_debug_test assert_test.cpp)
target_compile_options(support_assert_debug_test PRIVATE -UNDEBUG)
jnirt_add_test(support_assert_release_test assert_test.cpp)
target_compile_options(support_assert_release_test PRIVATE -DNDEBUG)

# The list of listeners is plain C++ and has threads of its own in the stress test: run the same file under TSan with
# -DCMAKE_CXX_FLAGS=-fsanitize=thread on the whole build.
jnirt_add_test(support_listener_set_test listener_set_test.cpp)
target_link_libraries(support_listener_set_test PRIVATE Threads::Threads)

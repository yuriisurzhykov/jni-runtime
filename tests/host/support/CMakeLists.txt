jnirt_add_test(support_logging_test logging_test.cpp)
jnirt_add_test(support_error_test error_test.cpp)

# The same assert tests in both build flavours: JNIRT_ASSERT depends on NDEBUG of the caller's translation unit.
jnirt_add_test(support_assert_debug_test assert_test.cpp)
target_compile_options(support_assert_debug_test PRIVATE -UNDEBUG)
jnirt_add_test(support_assert_release_test assert_test.cpp)
target_compile_options(support_assert_release_test PRIVATE -DNDEBUG)

cmake_minimum_required(VERSION 3.22)
project(jni_runtime VERSION 0.1.0 LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/jnirtWarnings.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/jnirtExports.cmake)

# The static libraries end up inside a shared library (the consumer's .so), so they must be position independent.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(support)
add_subdirectory(runtime)

# Host tests: on by default only when jni-runtime is the top-level project (not when a consumer embeds it).
option(JNIRT_BUILD_TESTS "Build host unit tests" ${PROJECT_IS_TOP_LEVEL})
if (JNIRT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/host)
endif ()

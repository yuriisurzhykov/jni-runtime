cmake_minimum_required(VERSION 3.22)
project(jni_runtime LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/jnirtWarnings.cmake)

add_subdirectory(support)
add_subdirectory(runtime)

# Host tests: on by default only when jni-runtime is the top-level project (not when a consumer embeds it).
option(JNIRT_BUILD_TESTS "Build host unit tests" ${PROJECT_IS_TOP_LEVEL})
if (JNIRT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/host)
endif ()

# Learning examples (examples/): off by default.
option(JNIRT_BUILD_EXAMPLES "Build the learning examples" OFF)
if (JNIRT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif ()

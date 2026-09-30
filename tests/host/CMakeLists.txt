# Host tests for jnirt. gtest comes from the system (apt: libgtest-dev) or, failing that, from the network.
find_package(GTest QUIET)
if (NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz)
    FetchContent_MakeAvailable(googletest)
endif ()
include(GoogleTest)

# jnirt_add_test(<name> <sources...>): one executable per test file, registered with ctest.
function(jnirt_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE jnirt::support jnirt::options GTest::gtest_main)
    gtest_discover_tests(${name})
endfunction()

add_subdirectory(support)

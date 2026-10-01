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

# Java classes for the tests that need a real JVM (class_cache, registry, init): compiled with javac, found through
# JNIRT_TEST_CLASSPATH. jnirt/NativeLoader.java stands in for the Kotlin NativeLoader and gets the runtime's version.
find_package(Java REQUIRED COMPONENTS Development)
set(JNIRT_TEST_CLASSES_DIR ${CMAKE_CURRENT_BINARY_DIR}/classes)
set(JNIRT_TEST_GENERATED_JAVA ${CMAKE_CURRENT_BINARY_DIR}/java/jnirt/NativeLoader.java)
configure_file(java/jnirt/NativeLoader.java.in ${JNIRT_TEST_GENERATED_JAVA} @ONLY)
file(GLOB_RECURSE JNIRT_TEST_JAVA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/java/*.java)
add_custom_command(
        OUTPUT ${JNIRT_TEST_CLASSES_DIR}/.compiled
        COMMAND ${CMAKE_COMMAND} -E make_directory ${JNIRT_TEST_CLASSES_DIR}
        COMMAND ${Java_JAVAC_EXECUTABLE} -d ${JNIRT_TEST_CLASSES_DIR} ${JNIRT_TEST_JAVA_SOURCES} ${JNIRT_TEST_GENERATED_JAVA}
        COMMAND ${CMAKE_COMMAND} -E touch ${JNIRT_TEST_CLASSES_DIR}/.compiled
        DEPENDS ${JNIRT_TEST_JAVA_SOURCES} ${JNIRT_TEST_GENERATED_JAVA}
)
add_custom_target(jnirt_test_classes DEPENDS ${JNIRT_TEST_CLASSES_DIR}/.compiled)

add_subdirectory(support)
add_subdirectory(runtime)

# What must not compile (ADR-0008 "Tests"): one ctest test per file in compile_fail/. Each file has a `// EXPECT: <text>` line
# with the message of jnirt's static_assert that must appear. The compiler is run with -fsyntax-only by compile_fail.cmake.
file(GLOB JNIRT_COMPILE_FAIL_CASES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/*.cpp)
foreach (case IN LISTS JNIRT_COMPILE_FAIL_CASES)
    get_filename_component(case_name ${case} NAME_WE)
    add_test(NAME CompileFail.${case_name}
            COMMAND ${CMAKE_COMMAND}
            -DCXX=${CMAKE_CXX_COMPILER}
            "-DINCLUDES=$<JOIN:$<TARGET_PROPERTY:jnirt_runtime,INTERFACE_INCLUDE_DIRECTORIES>,|>|$<JOIN:$<TARGET_PROPERTY:jnirt_support,INTERFACE_INCLUDE_DIRECTORIES>,|>"
            -DSOURCE=${case}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/compile_fail.cmake)
endforeach ()

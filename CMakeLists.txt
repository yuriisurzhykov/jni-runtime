cmake_minimum_required(VERSION 3.22)
project(jni_runtime LANGUAGES CXX)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/jnirtWarnings.cmake)

add_subdirectory(support)
add_subdirectory(runtime)
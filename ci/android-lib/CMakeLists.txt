# What a consumer's app module does, as small as possible: the one .so with the runtime inside. CI builds it with the
# NDK and runs tools/check-native-lib.sh on the result (.github/workflows/ci.yml, job android-binary).
cmake_minimum_required(VERSION 3.22)
project(jnirt_android_lib LANGUAGES CXX)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../.. jnirt)

add_library(jnirt_sample SHARED onload.cpp)
target_link_libraries(jnirt_sample PRIVATE jnirt::runtime jnirt::options)
jnirt_restrict_exports(jnirt_sample)

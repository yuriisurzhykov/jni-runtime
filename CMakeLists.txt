cmake_minimum_required(VERSION 3.22)

target_include_directories(jnirt::runtime
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/runtime>
)

target_include_directories(jnirt::support
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/support>
)
cmake_minimum_required(VERSION 3.22)
project(jni_runtime LANGUAGES CXX)

add_library(jnirt STATIC
        src/support/logging/logging_core.cpp
        # ... the rest of the source code (runtime, etc.)
)

target_compile_features(jnirt PUBLIC cxx_std_17)

target_include_directories(jnirt
        PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Choose the platform log sink
if (ANDROID)
    target_sources(jnirt PRIVATE src/android/logging/log_sink_android.cpp)
    target_link_libraries(jnirt PRIVATE log)
else ()
    message(FATAL_ERROR "jnirt: no logging sink for this platform")
endif ()

target_compile_options(jnirt PRIVATE -Wall -Wextra -Wformat -Werror=format-security)
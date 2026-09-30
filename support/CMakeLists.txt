add_library(jnirt_support STATIC
        src/assert.cpp
        src/error.cpp
        src/format.cpp
        src/logging/logging_core.cpp
)
add_library(jnirt::support ALIAS jnirt_support)

target_compile_features(jnirt_support
        PUBLIC cxx_std_17                       # public headers need C++17
)

target_include_directories(jnirt_support
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include      # <jnirt/support/...> for everyone
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src         # log_sink.h stays internal
)

target_link_libraries(jnirt_support
        PRIVATE jnirt::options
)

# Platform parts (log sink, abort hook): exactly one implementation per platform.
if (ANDROID)
    target_sources(jnirt_support PRIVATE
            src/android/logging/log_sink_android.cpp
            src/android/abort_android.cpp)
    target_link_libraries(jnirt_support PRIVATE log)   # liblog
else ()
    target_sources(jnirt_support PRIVATE       # host build and tests
            src/host/logging/log_sink_stderr.cpp
            src/host/abort_host.cpp)
endif ()

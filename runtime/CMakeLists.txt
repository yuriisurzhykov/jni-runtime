add_library(jnirt_runtime STATIC
        src/env.cpp
        src/vm.cpp
)
add_library(jnirt::runtime ALIAS jnirt_runtime)

target_include_directories(jnirt_runtime
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(jnirt_runtime
        PUBLIC jnirt::support          # runtime -> support, never back
        PRIVATE jnirt::options
)

# jni.h: the NDK sysroot has it on Android. On the host it comes from the JDK (needed for host tests).
if (NOT ANDROID)
    find_package(JNI REQUIRED)
    target_include_directories(jnirt_runtime PUBLIC ${JNI_INCLUDE_DIRS})
    find_package(Threads REQUIRED)
    target_link_libraries(jnirt_runtime PRIVATE Threads::Threads)
endif ()

# Learning examples, not part of the library. Enabled with -DJNIRT_BUILD_EXAMPLES=ON (host only).
add_executable(refs_playground refs_playground.cpp)
target_link_libraries(refs_playground PRIVATE jnirt::runtime jnirt::options ${JAVA_JVM_LIBRARY})

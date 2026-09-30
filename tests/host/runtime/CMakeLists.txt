jnirt_add_test(runtime_vm_test vm_test.cpp)
target_link_libraries(runtime_vm_test PRIVATE jnirt::runtime)

# Starts a real JVM, so it links libjvm (found by find_package(JNI) in runtime/).
jnirt_add_test(runtime_env_test env_test.cpp)
target_link_libraries(runtime_env_test PRIVATE jnirt::runtime ${JAVA_JVM_LIBRARY})

jnirt_add_test(runtime_vm_test vm_test.cpp)
target_link_libraries(runtime_vm_test PRIVATE jnirt::runtime)

# Starts a real JVM, so it links libjvm (found by find_package(JNI) in runtime/).
jnirt_add_test(runtime_env_test env_test.cpp)
target_link_libraries(runtime_env_test PRIVATE jnirt::runtime ${JAVA_JVM_LIBRARY})

jnirt_add_test(runtime_ref_test ref_test.cpp)
target_link_libraries(runtime_ref_test PRIVATE jnirt::runtime ${JAVA_JVM_LIBRARY})
# Reads the reference counters from the runtime's internal header.
target_include_directories(runtime_ref_test PRIVATE ${PROJECT_SOURCE_DIR}/runtime/src)

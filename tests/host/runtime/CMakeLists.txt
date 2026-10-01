jnirt_add_test(runtime_vm_test vm_test.cpp)
target_link_libraries(runtime_vm_test PRIVATE jnirt::runtime)

# Starts a real JVM, so it links libjvm (found by find_package(JNI) in runtime/).
jnirt_add_test(runtime_env_test env_test.cpp)
target_link_libraries(runtime_env_test PRIVATE jnirt::runtime ${JAVA_JVM_LIBRARY})

jnirt_add_test(runtime_ref_test ref_test.cpp)
target_link_libraries(runtime_ref_test PRIVATE jnirt::runtime ${JAVA_JVM_LIBRARY})
# Reads the reference counters from the runtime's internal header.
target_include_directories(runtime_ref_test PRIVATE ${PROJECT_SOURCE_DIR}/runtime/src)

# The static_asserts need no JVM; the lookups at the bottom of the file start one.
jnirt_add_test(runtime_signature_test signature_test.cpp)
target_link_libraries(runtime_signature_test PRIVATE jnirt::runtime ${JAVA_JVM_LIBRARY})

# class_cache, registry and init against a real JVM with the Java classes of tests/host/java.
function(jnirt_add_jvm_test name source)
    jnirt_add_test(${name} ${source})
    target_link_libraries(${name} PRIVATE jnirt::runtime ${JAVA_JVM_LIBRARY})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/runtime/src)   # the startup steps and counters
    target_compile_definitions(${name} PRIVATE JNIRT_TEST_CLASSPATH="${JNIRT_TEST_CLASSES_DIR}")
    add_dependencies(${name} jnirt_test_classes)
endfunction()

jnirt_add_jvm_test(runtime_class_cache_test class_cache_test.cpp)
jnirt_add_jvm_test(runtime_registry_test registry_test.cpp)
jnirt_add_jvm_test(runtime_init_test init_test.cpp)
jnirt_add_jvm_test(runtime_entry_test entry_test.cpp)

# The domain check works on lists: no JVM.
jnirt_add_test(runtime_domains_test domains_test.cpp)
target_link_libraries(runtime_domains_test PRIVATE jnirt::runtime)
target_include_directories(runtime_domains_test PRIVATE ${PROJECT_SOURCE_DIR}/runtime/src)

jnirt_add_jvm_test(runtime_log_bridge_test log_bridge_test.cpp)

jnirt_add_jvm_test(runtime_convert_test convert_test.cpp)
jnirt_add_jvm_test(runtime_convert_array_test convert_array_test.cpp)

// Breaks the rule on purpose: a second exported symbol.
#include <jnirt/runtime/registry.h>

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    return jnirt::init(vm, {});
}

extern "C" __attribute__((visibility("default"))) int jnirt_test_leaked_symbol() { return 1; }

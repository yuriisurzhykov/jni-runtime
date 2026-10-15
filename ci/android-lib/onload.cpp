#include <jnirt/runtime/registry.h>

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    return jnirt::init(vm, {});
}

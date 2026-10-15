// A library the way a consumer builds it: the runtime linked in, JNI_OnLoad as the only export.
#include <jnirt/runtime/registry.h>

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
    return jnirt::init(vm, {});
}

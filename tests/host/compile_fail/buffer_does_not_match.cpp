// EXPECT: the buffer does not match the array
#include <jnirt/runtime/class_cache.h>
void f(JNIEnv *env, jintArray a) { jlong x[2]; jnirt::readArray(env, a, 0, x, 2); }

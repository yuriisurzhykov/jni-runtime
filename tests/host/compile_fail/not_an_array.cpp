// EXPECT: this needs a JNI array of primitives
#include <jnirt/runtime/class_cache.h>
void f(JNIEnv *env, jobject a) { jint x[2]; jnirt::readArray(env, a, 0, x, 2); }

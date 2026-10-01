// EXPECT: a DirectBuffer can only come from Java
#include <jnirt/runtime/class_cache.h>
void f(JNIEnv *env, jnirt::DirectBuffer b) { jnirt::toJava(env, b); }

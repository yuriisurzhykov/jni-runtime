// EXPECT: a NativeBuffer is the result of a native function, not a parameter
#include <jnirt/runtime/native_buffer.h>
void f(JNIEnv *env, jobject o) { jnirt::fromJava<jnirt::NativeBuffer>(env, o); }

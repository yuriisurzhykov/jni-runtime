// EXPECT: this value points into a Java object
#include <jnirt/runtime/class_cache.h>
struct CJ : jnirt::JavaClass<CJ> { static constexpr const char *name = "a/C"; static inline jnirt::Method<CJ, jnirt::DirectBuffer()> get{"get"}; };
void f(JNIEnv *env, jobject o) { CJ::get(env, o); }

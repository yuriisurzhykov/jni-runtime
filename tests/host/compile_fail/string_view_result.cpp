// EXPECT: std::string_view can only be given to Java
#include <jnirt/runtime/class_cache.h>
struct CJ : jnirt::JavaClass<CJ> { static constexpr const char *name = "a/C"; static inline jnirt::Method<CJ, std::string_view()> get{"get"}; };
std::string_view f(JNIEnv *env, jobject o) { return CJ::get(env, o); }

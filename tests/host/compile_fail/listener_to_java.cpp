// EXPECT: a Listener is a parameter of a native function
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/listener.h>
struct LJ : jnirt::JavaClass<LJ> { static constexpr const char *name = "a/L"; };
struct CJ : jnirt::JavaClass<CJ> { static constexpr const char *name = "a/C"; static inline jnirt::Method<CJ, void(LJ)> take{"take"}; };
void f(JNIEnv *env, jobject o, jnirt::Listener<LJ> l) { jnirt::Method<CJ, void(jnirt::Listener<LJ>)> m{"x"}; m(env, o, l); }

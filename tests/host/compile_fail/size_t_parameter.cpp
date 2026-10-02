// EXPECT: Java has no unsigned numbers
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/registry.h>
void f(JNIEnv *, jobject, std::size_t) {}
void reg(jnirt::Registry &r);
struct CJ : jnirt::JavaClass<CJ> { static constexpr const char *name = "a/C"; };
void reg(jnirt::Registry &r) { r.add<CJ>().native<&f>("f"); }

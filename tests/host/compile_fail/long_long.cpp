// EXPECT: this integer type is not one of the JNI types
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
constexpr auto d = jnirt::descriptor<long long>;

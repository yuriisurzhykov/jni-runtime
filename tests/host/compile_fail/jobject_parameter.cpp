// EXPECT: jobject does not say which Java class
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
constexpr auto d = jnirt::descriptor<jobject>;

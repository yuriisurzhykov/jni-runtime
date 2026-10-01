// EXPECT: jobjectArray does not say the element class
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
constexpr auto d = jnirt::descriptor<jobjectArray>;

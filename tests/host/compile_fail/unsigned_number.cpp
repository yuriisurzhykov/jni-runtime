// EXPECT: Java has no unsigned numbers
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
constexpr auto d = jnirt::descriptor<unsigned>;

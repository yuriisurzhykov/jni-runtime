// EXPECT: std::optional of a raw JNI reference
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
constexpr auto d = jnirt::descriptor<std::optional<jstring>>;

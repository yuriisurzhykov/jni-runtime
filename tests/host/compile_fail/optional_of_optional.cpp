// EXPECT: std::optional<std::optional<T>> has no Java meaning
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
constexpr auto d = jnirt::descriptor<std::optional<std::optional<jint>>>;

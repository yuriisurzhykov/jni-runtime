// EXPECT: std::vector<uint8_t> is std::vector<jboolean>
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
#include <cstdint>
constexpr auto d = jnirt::descriptor<std::vector<std::uint8_t>>;

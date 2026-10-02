// EXPECT: this C++ type has no Java type
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
struct Frame { int index; };
constexpr auto d = jnirt::descriptor<Frame>;

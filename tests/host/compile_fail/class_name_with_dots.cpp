// EXPECT: JavaClass name must be in JVM form
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/signature.h>
struct CJ : jnirt::JavaClass<CJ> { static constexpr const char *name = "com.acme.Camera"; };
constexpr auto d = jnirt::descriptor<CJ>;

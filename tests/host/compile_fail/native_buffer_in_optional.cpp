// EXPECT: a Peer can not be inside std::optional
#include <jnirt/runtime/native_buffer.h>
#include <jnirt/runtime/signature.h>
constexpr auto d = jnirt::descriptor<std::optional<jnirt::NativeBuffer>>;

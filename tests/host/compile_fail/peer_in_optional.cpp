// EXPECT: a Peer can not be inside std::optional
#include <jnirt/runtime/peer.h>
#include <jnirt/runtime/signature.h>
struct Cam {};
constexpr auto d = jnirt::descriptor<std::optional<jnirt::Peer<Cam>>>;

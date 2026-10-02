// EXPECT: a Peer can not be an element of a collection
#include <jnirt/runtime/peer.h>
#include <jnirt/runtime/signature.h>
#include <vector>
struct Cam {};
constexpr auto d = jnirt::descriptor<std::vector<jnirt::Peer<Cam>>>;

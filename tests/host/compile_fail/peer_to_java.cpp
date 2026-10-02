// EXPECT: a Peer is a native parameter or result and nothing else
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/peer.h>
struct Cam {};
struct CJ : jnirt::JavaClass<CJ> { static constexpr const char *name = "a/C"; static inline jnirt::Method<CJ, void(jnirt::Peer<Cam>)> take{"take"}; };
void f(JNIEnv *env, jobject o, jnirt::Peer<Cam> p) { CJ::take(env, o, p); }

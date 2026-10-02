// The non-template part of Listener<J> (listener.h): the local frame of one event and the log line for a failing listener.
//
// Created by Yurii Surzhykov on 10/2/26.

#include <jnirt/runtime/listener.h>
#include <jnirt/support/logging/macros.h>

LOG_DEFINE_TAG("jnirt")

namespace jnirt::detail {
    namespace {
        // The local references of one event: the arguments and the ByteBuffer. Generous; a listener with more is rare.
        constexpr jint kFrameCapacity = 16;
    } // namespace

    bool pushFrame(JNIEnv *env) noexcept {
        if (env->PushLocalFrame(kFrameCapacity) == 0) return true;
        env->ExceptionClear(); // OutOfMemoryError
        LOGE("no room for a local frame: an event of a listener was dropped");
        return false;
    }

    void popFrame(JNIEnv *env) noexcept { env->PopLocalFrame(nullptr); }

    void logListenerFailure(const char *javaClass, const char *what) noexcept {
        LOGE("a listener (%s) threw, the others still get the event: %s", javaClass, what);
    }
} // namespace jnirt::detail

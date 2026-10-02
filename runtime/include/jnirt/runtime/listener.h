// A Kotlin listener held by native code and called from any thread. See docs/adr/0010-callbacks-and-subscriptions.md
// and docs/learning/callbacks-by-example.md.
//
//   struct FrameListenerJ : jnirt::JavaClass<FrameListenerJ> {
//       static constexpr const char *name = "com/acme/camera/FrameListener";
//       static inline jnirt::Method<FrameListenerJ, void(jnirt::NativeBuffer)> onFrame{"onFrame"};
//   };
//
//   Peer<Subscription> subscribe(JNIEnv *, jclass, Peer<Camera> self, Listener<FrameListenerJ> listener) {
//       return makeSubscription(self->subscribe([listener](const Event &e) {      // the lambda keeps the listener alive
//           listener.call(FrameListenerJ::onFrame, NativeBuffer(e.frame, e.frame->data(), e.frame->size()));
//       }));
//   }
//
// As a parameter it is a `GlobalRef` to the Kotlin object (a local one would be gone when subscribe returns). The
// reference is strong: it keeps the listener alive until the last copy of this object dies, which is when the
// subscription is closed (ADR-0010 §2). Copies share one reference.
//
// call() works on any thread. It attaches the thread when needed, opens a local frame so that the references of one
// event are freed with it (a thread that is not in a call from Java never frees local references by itself), calls the
// method through class_cache, and logs a Java exception instead of throwing it: one failing listener must not stop the
// native thread that serves the others. Other C++ exceptions pass through to the caller (ListenerSet::emit catches).
//
// Created by Yurii Surzhykov on 10/2/26.

#ifndef JNI_RUNTIME_LISTENER_H
#define JNI_RUNTIME_LISTENER_H

#pragma once
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/convert.h>
#include <jnirt/runtime/env.h>
#include <jnirt/runtime/exception.h>
#include <jnirt/runtime/ref.h>

#include <jni.h>

#include <memory>
#include <utility>

namespace jnirt {
    namespace detail {
        // PushLocalFrame / PopLocalFrame. pushFrame returns false (and logs) when there is no room for a frame.
        bool pushFrame(JNIEnv *env) noexcept;

        void popFrame(JNIEnv *env) noexcept;

        void logListenerFailure(const char *javaClass, const char *what) noexcept;

        struct LocalFrame {
            explicit LocalFrame(JNIEnv *env) noexcept : env(env), pushed(pushFrame(env)) {
            }

            ~LocalFrame() {
                if (pushed) popFrame(env);
            }

            LocalFrame(const LocalFrame &) = delete;

            LocalFrame &operator=(const LocalFrame &) = delete;

            JNIEnv *env;
            bool pushed;
        };
    } // namespace detail

    // J is the description of the Java interface (struct J : JavaClass<J>).
    template<class J>
    class Listener {
    public:
        Listener() noexcept = default;

        explicit Listener(jobject object) : ref_(std::make_shared<GlobalRef<jobject> >(object)) {
        }

        explicit operator bool() const noexcept { return ref_ != nullptr && static_cast<bool>(*ref_); }

        // `method` is a Method of J (J::onFrame), `args` are what that method takes.
        template<class Method, class... Args>
        void call(const Method &method, Args &&... args) const {
            JNIEnv *env = jnirt::env();
            detail::LocalFrame frame(env);
            if (!frame.pushed) return;
            try {
                method(env, ref_->get(), std::forward<Args>(args)...);
            } catch (const JavaException &e) {
                detail::logListenerFailure(J::name, e.what());
            }
        }

    private:
        std::shared_ptr<GlobalRef<jobject> > ref_;
    };

    // The row of the table: a Listener is the Java interface J. A parameter of a native and nothing else.
    template<class J>
    struct Convert<Listener<J> > : ConvertObject<Listener<J>, J> {
        static Listener<J> fromJava(JNIEnv *, jobject object) { return Listener<J>(object); }

        template<class Never = void>
        static LocalRef<jobject> toJava(JNIEnv *, const Listener<J> &) {
            static_assert(detail::AlwaysFalse<Never>::value,
                          "jnirt: a Listener is a parameter of a native function, not a result or an argument of a call "
                          "into Java: Java already has its listener, and a copy of it would be a new reference for "
                          "nothing.");
            return {};
        }
    };
} // namespace jnirt

#endif //JNI_RUNTIME_LISTENER_H

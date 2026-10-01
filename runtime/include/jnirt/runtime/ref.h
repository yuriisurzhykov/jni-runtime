// Owning wrappers for JNI references. See docs/adr/0004-references.md and docs/learning/thinking-in-jnirt.md.
//
//   LocalRef<T>       a local reference JNI returned to us; this thread, this scope
//   GlobalRef<T>      a strong reference that outlives the call; any thread
//   WeakGlobalRef<T>  a reference that does not keep the object alive; reach it with promote(env)
//
// All three delete their reference in the destructor, can be moved and can not be copied: one wrapper owns one
// JNI reference. T is the JNI type (jobject, jclass, jstring, jobjectArray, ...), so get() needs no cast.
//
//   jnirt::LocalRef<jclass> cls(env, env->FindClass("java/lang/String"));
//   jnirt::GlobalRef<jobject> listener(listenerParam);          // keep it after the call returns
//   jnirt::WeakGlobalRef<jobject> owner(thiz);                  // point back to the Kotlin owner
//   if (auto obj = owner.promote(env)) { use(obj.get()); }      // alive: held until the end of the block
//   return result.release();                                    // hand a local reference back to Java
//
// Never put GlobalRef / WeakGlobalRef in static or global storage: static destructors run during exit(), when the
// VM may be gone. Long-lived references live in class_cache.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNIRT_RUNTIME_REF_H
#define JNIRT_RUNTIME_REF_H

#pragma once
#include <jni.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace jnirt {
    namespace detail {
        // Not for direct use. Debug bookkeeping behind the wrappers below: in release builds the wrappers do not
        // call the LocalRef hooks, and the global and weak counters stay at zero.
        void onLocalRefCreated() noexcept;
        void onLocalRefGone(JNIEnv *owner) noexcept; // also checks that `owner` is this thread's env

        // NewGlobalRef / NewWeakGlobalRef through jnirt::env(). nullptr in, nullptr out; a failed non-null one
        // is a crash.
        jobject newGlobalRef(jobject ref);
        void deleteGlobalRef(jobject ref) noexcept;
        jweak newWeakGlobalRef(jobject ref);
        void deleteWeakGlobalRef(jweak ref) noexcept;

        // Live references, for tests and for detachCurrentThread(). Always 0 in release builds.
        int liveLocalRefsOnThisThread() noexcept;
        std::size_t liveGlobalRefs() noexcept;
        std::size_t liveWeakGlobalRefs() noexcept;

        template<class T>
        constexpr bool isReferenceType = std::is_convertible_v<T, jobject>;
    } // namespace detail

    // A local reference that JNI returned to us: FindClass, NewObject, Call*ObjectMethod, GetObjectArrayElement,
    // NewStringUTF, ... Do not wrap parameters of a native method: the JVM owns them.
    // Keeps the env it was given (it belongs to this thread and this scope) and deletes through it.
    template<class T = jobject>
    class LocalRef {
        static_assert(detail::isReferenceType<T>, "LocalRef<T>: T must be a JNI reference type (jobject, jclass, ...)");

    public:
        LocalRef() noexcept = default;

        LocalRef(JNIEnv *env, T ref) noexcept : env_(ref != nullptr ? env : nullptr), ref_(ref) {
#ifndef NDEBUG
            if (ref_ != nullptr) detail::onLocalRefCreated();
#endif
        }

        LocalRef(LocalRef &&other) noexcept
            : env_(std::exchange(other.env_, nullptr)), ref_(std::exchange(other.ref_, nullptr)) {}

        LocalRef &operator=(LocalRef &&other) noexcept {
            if (this != &other) {
                reset();
                env_ = std::exchange(other.env_, nullptr);
                ref_ = std::exchange(other.ref_, nullptr);
            }
            return *this;
        }

        LocalRef(const LocalRef &) = delete;
        LocalRef &operator=(const LocalRef &) = delete;

        ~LocalRef() { reset(); }

        T get() const noexcept { return ref_; }
        explicit operator bool() const noexcept { return ref_ != nullptr; }

        // Deletes the reference now. The wrapper becomes empty.
        void reset() noexcept {
            if (ref_ == nullptr) return;
#ifndef NDEBUG
            detail::onLocalRefGone(env_);
#endif
            env_->DeleteLocalRef(ref_);
            ref_ = nullptr;
            env_ = nullptr;
        }

        // Gives the reference away without deleting it. This is how an object is returned to Java:
        // `return result.release();` (returning get() would delete it before the JVM reads it).
        [[nodiscard]] T release() noexcept {
#ifndef NDEBUG
            if (ref_ != nullptr) detail::onLocalRefGone(env_);
#endif
            env_ = nullptr;
            return std::exchange(ref_, nullptr);
        }

    private:
        JNIEnv *env_ = nullptr;
        T ref_ = nullptr;
    };

    // A strong reference that outlives the native call and can be used and destroyed on any thread.
    // Created and deleted through jnirt::env(), so the destructor may attach an unknown native thread
    // (it is detached again when the thread exits).
    template<class T = jobject>
    class GlobalRef {
        static_assert(detail::isReferenceType<T>, "GlobalRef<T>: T must be a JNI reference type (jobject, jclass, ...)");

    public:
        GlobalRef() noexcept = default;

        // Makes a new global reference to the same object as `ref` (a local, a parameter, another global).
        // nullptr gives an empty GlobalRef.
        explicit GlobalRef(T ref) : ref_(static_cast<T>(detail::newGlobalRef(ref))) {}

        GlobalRef(GlobalRef &&other) noexcept : ref_(std::exchange(other.ref_, nullptr)) {}

        GlobalRef &operator=(GlobalRef &&other) noexcept {
            if (this != &other) {
                reset();
                ref_ = std::exchange(other.ref_, nullptr);
            }
            return *this;
        }

        GlobalRef(const GlobalRef &) = delete;
        GlobalRef &operator=(const GlobalRef &) = delete;

        ~GlobalRef() { reset(); }

        T get() const noexcept { return ref_; }
        explicit operator bool() const noexcept { return ref_ != nullptr; }

        void reset() noexcept {
            if (ref_ != nullptr) detail::deleteGlobalRef(std::exchange(ref_, nullptr));
        }

    private:
        T ref_ = nullptr;
    };

    // A reference that does not keep the object alive. There is no get() on purpose: the only way to the object
    // is promote(env), which checks and holds it in one step. "Is it alive? then use it" races with the GC.
    template<class T = jobject>
    class WeakGlobalRef {
        static_assert(detail::isReferenceType<T>,
                      "WeakGlobalRef<T>: T must be a JNI reference type (jobject, jclass, ...)");

    public:
        WeakGlobalRef() noexcept = default;

        // nullptr gives an empty WeakGlobalRef.
        explicit WeakGlobalRef(T ref) : ref_(detail::newWeakGlobalRef(ref)) {}

        WeakGlobalRef(WeakGlobalRef &&other) noexcept : ref_(std::exchange(other.ref_, nullptr)) {}

        WeakGlobalRef &operator=(WeakGlobalRef &&other) noexcept {
            if (this != &other) {
                reset();
                ref_ = std::exchange(other.ref_, nullptr);
            }
            return *this;
        }

        WeakGlobalRef(const WeakGlobalRef &) = delete;
        WeakGlobalRef &operator=(const WeakGlobalRef &) = delete;

        ~WeakGlobalRef() { reset(); }

        // True if a reference is held, NOT that the object is still alive. Use promote() for that.
        explicit operator bool() const noexcept { return ref_ != nullptr; }

        // A local reference to the object while it is alive, or an empty LocalRef if it was collected (or this
        // WeakGlobalRef is empty). The object stays alive while the returned LocalRef does.
        LocalRef<T> promote(JNIEnv *env) const {
            if (ref_ == nullptr) return {};
            return LocalRef<T>(env, static_cast<T>(env->NewLocalRef(ref_)));
        }

        void reset() noexcept {
            if (ref_ != nullptr) detail::deleteWeakGlobalRef(std::exchange(ref_, nullptr));
        }

    private:
        jweak ref_ = nullptr;
    };
} // namespace jnirt

#endif //JNIRT_RUNTIME_REF_H

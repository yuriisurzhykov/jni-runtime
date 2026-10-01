// JavaException: a Java exception seen from C++. See <jnirt/runtime/exception.h>.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/exception.h>

#include "class_cache_internal.h"

#include <cstring>
#include <utility>

namespace jnirt {
    namespace {
        // The two members the runtime needs to put a Java exception into words. Described like any user class, so
        // the lookup, the error messages and the debug checks are the ones the user's classes get.
        struct ThrowableJ : JavaClass<ThrowableJ> {
            static constexpr const char *name = "java/lang/Throwable";

            static inline Method<ThrowableJ, jstring()> toString{"toString"};
            static inline Method<ThrowableJ, ThrowableJ()> getCause{"getCause"};
        };

        // A static initializer that failed is reported by FindClass as ExceptionInInitializerError, whose
        // toString() says nothing; the interesting part is its cause.
        constexpr const char *kInitializerError = "java.lang.ExceptionInInitializerError";

        std::string describeOnce(JNIEnv *env, jthrowable throwable) {
            std::string text = detail::toUtf8(env, ThrowableJ::toString(env, throwable).get());
            if (text.compare(0, std::strlen(kInitializerError), kInitializerError) != 0) return text;
            auto cause = ThrowableJ::getCause(env, throwable);
            if (cause) text += ", caused by " + detail::toUtf8(env, ThrowableJ::toString(env, cause.get()).get());
            return text;
        }
    } // namespace

    JavaException::JavaException(std::shared_ptr<const State> state) noexcept : state_(std::move(state)) {}

    JavaException::~JavaException() = default;

    jthrowable JavaException::throwable() const noexcept { return state_->throwable.get(); }

    const char *JavaException::what() const noexcept { return state_->text.c_str(); }

    namespace detail {
        std::string describeThrowable(JNIEnv *env, jthrowable throwable) {
            // toString() can throw too: the exception it throws is described by this function again, which must
            // not call toString() once more.
            thread_local bool describing = false;
            if (describing || !ThrowableJ::toString.resolved()) return "<a Java exception>";
            describing = true;
            try {
                std::string text = describeOnce(env, throwable);
                describing = false;
                return text;
            } catch (const JavaException &) {
                describing = false;
                return "<a Java exception whose toString() threw>";
            }
        }

        void throwPendingException(JNIEnv *env) {
            LocalRef<jthrowable> pending(env, env->ExceptionOccurred());
            env->ExceptionClear();
            std::string text = describeThrowable(env, pending.get());
            throw JavaException(std::make_shared<const JavaException::State>(
                JavaException::State{GlobalRef<jthrowable>(pending.get()), std::move(text)}));
        }

        void throwIfPending(JNIEnv *env) {
            if (env->ExceptionCheck()) throwPendingException(env);
        }

        ClassInfo &throwableClassInfo() { return classInfo<ThrowableJ>(); }
    } // namespace detail
} // namespace jnirt

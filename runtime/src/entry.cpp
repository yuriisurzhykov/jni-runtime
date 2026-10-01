// entry(): the C++ error to Java exception half of jnirt. See <jnirt/runtime/entry.h> and
// docs/adr/0007-exceptions-entry-and-log-sink.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/entry.h>
#include <jnirt/runtime/exception.h>
#include <jnirt/support/error.h>
#include <jnirt/support/logging/macros.h>

#include "class_cache_internal.h"
#include "runtime_classes.h"
#include "utf.h"

#include <cxxabi.h>

#include <cstdlib>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <typeinfo>

LOG_DEFINE_TAG("jnirt")

namespace jnirt::detail {
    namespace {
        // Exceptions of one chain that are passed on: the top one and its causes. A longer chain is cut, not an error.
        constexpr int kMaxChain = 8;

        std::string typeName(const std::type_info &type) {
            int status = 0;
            std::unique_ptr<char, void (*)(void *)> demangled(
                abi::__cxa_demangle(type.name(), nullptr, nullptr, &status), std::free);
            return status == 0 && demangled ? std::string(demangled.get()) : std::string(type.name());
        }

        // "camera.cpp" from "/home/me/project/src/camera.cpp": the place is useful, the build machine's paths are not.
        const char *fileName(const char *path) {
            const char *name = path;
            for (const char *c = path; *c != '\0'; ++c) {
                if (*c == '/' || *c == '\\') name = c + 1;
            }
            return name;
        }

        std::string withPlace(const ContractViolation &e) {
            return std::string(e.what()) + " (" + fileName(e.file()) + ":" + std::to_string(e.line()) + ")";
        }

        // A Java string from C++ text; a JVM that can not allocate it is a pending OutOfMemoryError, thrown on as
        // JavaException like any other Java failure.
        LocalRef<jstring> javaString(JNIEnv *env, const std::string &text) {
            LocalRef<jstring> string(env, newJavaString(env, text));
            if (!string) throwPendingException(env);
            return string;
        }

        LocalRef<jthrowable> asThrowable(JNIEnv *env, LocalRef<jobject> object) {
            return LocalRef<jthrowable>(env, static_cast<jthrowable>(object.release()));
        }

        LocalRef<jthrowable> build(JNIEnv *env, std::exception_ptr caught, int depth);

        // The exception that std::throw_with_nested put inside `e`, as a Java cause. Empty if there is none or the
        // chain is long enough.
        LocalRef<jthrowable> causeOf(JNIEnv *env, const std::exception &e, int depth) {
            if (depth + 1 >= kMaxChain) return {};
            auto *nested = dynamic_cast<const std::nested_exception *>(&e);
            if (nested == nullptr || nested->nested_ptr() == nullptr) return {};
            return build(env, nested->nested_ptr(), depth + 1);
        }

        // The Java exception for a C++ one, not thrown yet. Logs the top exception of a chain (depth 0), before any
        // Java exception is pending: the log may go to Java.
        LocalRef<jthrowable> build(JNIEnv *env, std::exception_ptr caught, int depth) {
            try {
                std::rethrow_exception(caught);
            } catch (const JavaException &e) {
                // Java threw it, native code did not catch it: Kotlin gets the very same object back.
                return LocalRef<jthrowable>(env, static_cast<jthrowable>(env->NewLocalRef(e.throwable())));
            } catch (const IllegalArgument &e) {
                std::string text = withPlace(e);
                if (depth == 0) LOGW("IllegalArgumentException: %s", text.c_str());
                auto cause = causeOf(env, e, depth);
                return asThrowable(env, IllegalArgumentExceptionJ::create(env, javaString(env, text).get(), cause.get()));
            } catch (const IllegalState &e) {
                std::string text = withPlace(e);
                if (depth == 0) LOGW("IllegalStateException: %s", text.c_str());
                auto cause = causeOf(env, e, depth);
                return asThrowable(env, IllegalStateExceptionJ::create(env, javaString(env, text).get(), cause.get()));
            } catch (const Error &e) {
                if (depth == 0) LOGW("NativeException %s/%d: %s", e.domain(), e.code(), e.what());
                auto cause = causeOf(env, e, depth);
                return asThrowable(env, NativeExceptionJ::create(env, javaString(env, e.domain()).get(), e.code(),
                                                                 javaString(env, e.what()).get(), cause.get()));
            } catch (const std::bad_alloc &) {
                if (depth == 0) LOGE("OutOfMemoryError: native code ran out of memory");
                return asThrowable(env, OutOfMemoryErrorJ::create(env, javaString(env, "native code ran out of memory").get()));
            } catch (const std::exception &e) {
                std::string text = typeName(typeid(e)) + ": " + e.what();
                if (depth == 0) LOGE("unexpected C++ exception %s", text.c_str());
                auto cause = causeOf(env, e, depth);
                return asThrowable(env, NativeExceptionJ::create(env, javaString(env, "jnirt").get(),
                                                                 static_cast<jint>(Errc::Internal),
                                                                 javaString(env, text).get(), cause.get()));
            } catch (...) {
                if (depth == 0) LOGE("unknown C++ exception (not a std::exception)");
                return asThrowable(env, NativeExceptionJ::create(env, javaString(env, "jnirt").get(),
                                                                 static_cast<jint>(Errc::Internal),
                                                                 javaString(env, "unknown C++ exception").get(), nullptr));
            }
        }

        // For the log line when the exception can not be delivered.
        std::string describe(std::exception_ptr caught) {
            try {
                std::rethrow_exception(caught);
            } catch (const std::exception &e) {
                return typeName(typeid(e)) + ": " + e.what();
            } catch (...) {
                return "unknown C++ exception";
            }
        }

        void throwLastResort(JNIEnv *env) noexcept {
            jclass error = env->FindClass("java/lang/Error");
            if (error == nullptr) return; // the JVM is in a state where nothing more can be done
            env->ThrowNew(error, "jnirt could not turn a native exception into a Java one; see logcat, tag jnirt");
            env->DeleteLocalRef(error);
        }
    } // namespace

    void handleNativeException(JNIEnv *env) noexcept {
        try {
            std::exception_ptr caught = std::current_exception();
            if (env->ExceptionCheck()) {
                // Raw JNI code left a Java exception pending: it wins, the C++ one is only logged (ADR-0002 §5).
                LOGE("a C++ exception was dropped because a Java exception is already pending: %s",
                     describe(caught).c_str());
                return;
            }
            LocalRef<jthrowable> java;
            try {
                java = build(env, caught, 0);
            } catch (const JavaException &e) {
                // The JVM failed while the Java exception was built (usually OutOfMemoryError): that one is thrown.
                java = LocalRef<jthrowable>(env, static_cast<jthrowable>(env->NewLocalRef(e.throwable())));
            }
            if (!java || env->Throw(java.get()) != JNI_OK) throwLastResort(env);
        } catch (...) {
            // Nothing may leave: a C++ exception here would cross into the JVM, which is what this function prevents.
            throwLastResort(env);
        }
    }
} // namespace jnirt::detail

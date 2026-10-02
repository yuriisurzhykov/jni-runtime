// JNIEnv for the current thread: GetEnv first, attach if needed, detach at thread exit.
//
// Whether jnirt attached the current thread is kept in a pthread_key (its value is the JavaVM that attached it).
// The key's destructor runs when a thread exits with a non-null value and detaches the thread, which works for
// threads we do not own as well (SDK callback threads). This is the approach recommended by Android's JNI tips.
//
// Created by Yurii Surzhykov on 9/30/26.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/ref.h>
#include <jnirt/runtime/vm.h>
#include <jnirt/support/assert.h>

#include "class_cache_internal.h"
#include "critical.h"
#include "ref_internal.h"
#include "thread_name.h"
#include "vm_internal.h"

#include <pthread.h>

namespace jnirt {
    using detail::ThreadName;

    namespace {
        pthread_key_t gAttachedByUs;
        pthread_once_t gKeyOnce = PTHREAD_ONCE_INIT;

        void detachAtThreadExit(void *attachedVm) {
            static_cast<JavaVM *>(attachedVm)->DetachCurrentThread();
        }

        void createKey() {
            if (pthread_key_create(&gAttachedByUs, detachAtThreadExit) != 0) {
                JNIRT_FATAL("jnirt could not create its thread key.\n"
                    "  why: the process ran out of pthread keys (PTHREAD_KEYS_MAX).\n"
                    "  fix: none in jnirt; look for code that creates pthread keys in a loop.");
            }
        }

        pthread_key_t attachedByUsKey() {
            pthread_once(&gKeyOnce, createKey);
            return gAttachedByUs;
        }

        JNIEnv *getEnv(JavaVM *vm, jint *status) {
            JNIEnv *env = nullptr;
            *status = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
            return env;
        }

        JNIEnv *attach(JavaVM *vm) {
            ThreadName name;
            JavaVMAttachArgs args{JNI_VERSION_1_6, name.value[0] != '\0' ? name.value : nullptr, nullptr};

            JNIEnv *env = nullptr;
            // Android's jni.h takes JNIEnv**, the JDK's takes void**.
#ifdef __ANDROID__
            jint rc = vm->AttachCurrentThread(&env, &args);
#else
            jint rc = vm->AttachCurrentThread(reinterpret_cast<void **>(&env), &args);
#endif
            if (rc != JNI_OK || env == nullptr) {
                JNIRT_FATAL("could not attach thread \"%s\" to the JVM (error %d).\n"
                            "  why: the JVM is out of memory or shutting down.\n"
                            "  fix: none in the calling code; check memory usage and the log before this line.",
                            name.value, static_cast<int>(rc));
            }
            if (pthread_setspecific(attachedByUsKey(), vm) != 0) {
                JNIRT_FATAL("thread \"%s\" was attached, but jnirt could not remember it.\n"
                            "  why: out of memory while storing a thread-local value.\n"
                            "  fix: none in the calling code; check memory usage.", name.value);
            }
            return env;
        }
    } // namespace

    JNIEnv *env() {
        JNIRT_ASSERT(!detail::insideCritical(),
                     "jnirt::env() was called inside withCritical.\n"
                     "  why: between GetPrimitiveArrayCritical and its release no JNI call is allowed; the garbage "
                     "collector is held and a JNI call can deadlock the process.\n"
                     "  fix: do the work that needs JNI before or after withCritical, outside the lambda.");
        JavaVM *jvm = vm();
        jint status = JNI_OK;
        JNIEnv *current = getEnv(jvm, &status);
        switch (status) {
            case JNI_OK:
                return current; // a Java thread, or attached earlier by us or by anyone else
            case JNI_EDETACHED:
                return attach(jvm);
            default:
                JNIRT_FATAL("JavaVM::GetEnv failed with %d.\n"
                            "  why: the JVM does not support JNI 1.6 (JNI_EVERSION) or is broken.\n"
                            "  fix: none in the calling code; this should not happen on Android.",
                            static_cast<int>(status));
        }
    }

    namespace detail {
        JNIEnv *currentEnvOrNull() noexcept {
            JavaVM *jvm = vmOrNull();
            if (jvm == nullptr) return nullptr;
            jint status = JNI_OK;
            JNIEnv *current = getEnv(jvm, &status);
            return status == JNI_OK ? current : nullptr;
        }
    } // namespace detail

    void detachCurrentThread() {
        auto *attachedVm = static_cast<JavaVM *>(pthread_getspecific(attachedByUsKey()));
        if (attachedVm != nullptr) {
#ifndef NDEBUG
            if (int alive = detail::liveLocalRefsOnThisThread(); alive > 0) {
                JNIRT_FATAL("detachCurrentThread() called with %d LocalRef still alive on thread \"%s\".\n"
                            "  why: local references become invalid after detach; their destructors would use a "
                            "dead JNIEnv.\n"
                            "  fix: let LocalRef go out of scope before detaching (close the block first).",
                            alive, ThreadName().value);
            }
#endif
            // Clear first, so the key destructor does not detach a second time at thread exit.
            pthread_setspecific(attachedByUsKey(), nullptr);
            attachedVm->DetachCurrentThread();
            return;
        }

        JavaVM *jvm = detail::vmOrNull();
        if (jvm == nullptr) return; // no VM yet: this thread can not be attached at all
        jint status = JNI_OK;
        getEnv(jvm, &status);
        JNIRT_CHECK(status != JNI_OK,
                    "thread \"%s\" was attached by the JVM or by other code, not by jnirt.\n"
                    "  why: detaching a thread jnirt did not attach can break its owner (a Java thread dies).\n"
                    "  fix: only threads that jnirt attached can be detached; remove this call.",
                    ThreadName().value);
    }
} // namespace jnirt

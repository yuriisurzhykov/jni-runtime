// Non-template parts of LocalRef / GlobalRef / WeakGlobalRef: creation and deletion of global and weak references,
// and the debug bookkeeping. See docs/adr/0004-references.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/ref.h>
#include <jnirt/support/assert.h>

#include "thread_name.h"
#include "vm_internal.h"

#include <atomic>

namespace jnirt::detail {
    namespace {
        // Debug builds only. A plain thread_local int: each thread counts only its own local references.
        thread_local int tLiveLocalRefs = 0;
        std::atomic<std::size_t> gLiveGlobalRefs{0};
        std::atomic<std::size_t> gLiveWeakGlobalRefs{0};

        // The env of the current thread without attaching it: nullptr if the thread is not attached.
        JNIEnv *currentEnvOrNull() noexcept {
            JavaVM *jvm = vmOrNull();
            if (jvm == nullptr) return nullptr;
            JNIEnv *env = nullptr;
            if (jvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) return nullptr;
            return env;
        }

        void count([[maybe_unused]] std::atomic<std::size_t> &counter, [[maybe_unused]] bool up) noexcept {
#ifndef NDEBUG
            if (up) counter.fetch_add(1, std::memory_order_relaxed);
            else counter.fetch_sub(1, std::memory_order_relaxed);
#endif
        }
    } // namespace

    void onLocalRefCreated() noexcept {
        ++tLiveLocalRefs;
    }

    void onLocalRefGone(JNIEnv *owner) noexcept {
        if (currentEnvOrNull() != owner) {
            JNIRT_FATAL("a LocalRef is destroyed on thread \"%s\", not on the thread that created it.\n"
                        "  why: a local reference belongs to the thread that created it; another thread can not "
                        "delete it.\n"
                        "  fix: do not move LocalRef between threads; make a GlobalRef and pass that.",
                        ThreadName().value);
        }
        --tLiveLocalRefs;
    }

    jobject newGlobalRef(jobject ref) {
        if (ref == nullptr) return nullptr;
        jobject global = env()->NewGlobalRef(ref);
        if (global == nullptr) {
            JNIRT_FATAL("NewGlobalRef failed on thread \"%s\".\n"
                        "  why: the JVM is out of memory, or the reference passed in was a collected weak one.\n"
                        "  fix: reach weak references only through WeakGlobalRef::promote(); check memory usage.",
                        ThreadName().value);
        }
        count(gLiveGlobalRefs, true);
        return global;
    }

    void deleteGlobalRef(jobject ref) noexcept {
        env()->DeleteGlobalRef(ref);
        count(gLiveGlobalRefs, false);
    }

    jweak newWeakGlobalRef(jobject ref) {
        if (ref == nullptr) return nullptr;
        jweak weak = env()->NewWeakGlobalRef(ref);
        if (weak == nullptr) {
            JNIRT_FATAL("NewWeakGlobalRef failed on thread \"%s\".\n"
                        "  why: the JVM is out of memory, or the reference passed in was a collected weak one.\n"
                        "  fix: reach weak references only through WeakGlobalRef::promote(); check memory usage.",
                        ThreadName().value);
        }
        count(gLiveWeakGlobalRefs, true);
        return weak;
    }

    void deleteWeakGlobalRef(jweak ref) noexcept {
        env()->DeleteWeakGlobalRef(ref);
        count(gLiveWeakGlobalRefs, false);
    }

    int liveLocalRefsOnThisThread() noexcept {
        return tLiveLocalRefs;
    }

    std::size_t liveGlobalRefs() noexcept {
        return gLiveGlobalRefs.load(std::memory_order_relaxed);
    }

    std::size_t liveWeakGlobalRefs() noexcept {
        return gLiveWeakGlobalRefs.load(std::memory_order_relaxed);
    }
} // namespace jnirt::detail

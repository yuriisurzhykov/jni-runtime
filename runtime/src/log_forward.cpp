// Native log lines to Kotlin (NativeLogBridge), and the one native method jnirt registers itself. See
// docs/adr/0007-exceptions-entry-and-log-sink.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/support/assert.h>
#include <jnirt/support/logging/log.h>
#include <jnirt/support/logging/macros.h>

#include "class_cache_internal.h"
#include "registry_internal.h"
#include "runtime_classes.h"
#include "utf.h"
#include "vm_internal.h"

LOG_DEFINE_TAG("jnirt")

namespace jnirt::detail {
    namespace {
        // android.util.Log: VERBOSE 2, DEBUG 3, INFO 4, WARN 5, ERROR 6, ASSERT 7. Our Level is the same list from 0.
        constexpr jint kFirstPriority = 2;
        constexpr jint kLastPriority = 7;

        jint priorityOf(logging::Level level) { return static_cast<jint>(level) + kFirstPriority; }

        logging::Level levelOf(jint priority) {
            jint level = priority - kFirstPriority;
            if (level > static_cast<jint>(logging::Level::Error)) level = static_cast<jint>(logging::Level::Error);
            return static_cast<logging::Level>(level);
        }

        // NativeLogBridge.setNativeLevel(priority): what Kotlin calls to change the level of native logs.
        void setNativeLevel(JNIEnv *, jclass, jint priority) {
            JNIRT_REQUIRE(priority >= kFirstPriority && priority <= kLastPriority,
                          "priority must be an android.util.Log constant (VERBOSE 2 ... ASSERT 7), got %d",
                          static_cast<int>(priority));
            logging::setLevel(levelOf(priority));
        }
    } // namespace

    bool forwardLogToJava(logging::Level level, const char *tag, const char *message) noexcept {
        // A line written while a line is being forwarded (the receiver logs into native, or this function reports
        // a failure) goes straight to logcat.
        thread_local bool forwarding = false;
        if (forwarding) return false;

        JNIEnv *env = currentEnvOrNull();
        if (env == nullptr) return false; // a thread the JVM does not know: a log line does not attach it
        if (env->ExceptionCheck()) return false; // nothing may be called with a Java exception pending

        forwarding = true;
        bool taken = false;
        try {
            LocalRef<jstring> javaTag(env, newJavaString(env, tag));
            LocalRef<jstring> javaMessage(env, newJavaString(env, message));
            if (javaTag && javaMessage) {
                NativeLogBridgeJ::log(env, priorityOf(level), javaTag.get(), javaMessage.get());
                taken = true;
            } else {
                env->ExceptionClear(); // OutOfMemoryError: the line goes to logcat
            }
        } catch (const JavaException &e) {
            // The receiver threw. The exception is cleared; the line is not lost.
            LOGW("the log receiver threw %s", e.what());
        } catch (...) {
        }
        forwarding = false;
        return taken;
    }

    std::vector<ClassEntry> runtimeNatives() {
        Registry registry;
        registry.add<NativeLogBridgeJ>().native<&setNativeLevel>("setNativeLevel");
        return RegistryAccess::classes(registry);
    }
} // namespace jnirt::detail

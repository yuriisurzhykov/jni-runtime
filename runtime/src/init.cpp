// jnirt::init and the startup it runs. See docs/adr/0006-class-cache-and-registry.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/runtime/vm.h>
#include <jnirt/support/assert.h>
#include <jnirt/support/logging/macros.h>

#include "class_cache_internal.h"
#include "registry_internal.h"
#include "runtime_classes.h"

#include <algorithm>
#include <atomic>
#include <exception>

LOG_DEFINE_TAG("jnirt")

namespace jnirt {
    namespace detail {
        std::vector<ClassInfo *> runtimeClasses() {
            return {
                &throwableClassInfo(), &nativeLoaderClassInfo(), &classInfo<NativeExceptionJ>(),
                &classInfo<IllegalArgumentExceptionJ>(), &classInfo<IllegalStateExceptionJ>(),
                &classInfo<OutOfMemoryErrorJ>(), &classInfo<NativeLogBridgeJ>(),
                &classInfo<BooleanJ>(), &classInfo<ByteJ>(), &classInfo<CharacterJ>(), &classInfo<ShortJ>(),
                &classInfo<IntegerJ>(), &classInfo<LongJ>(), &classInfo<FloatJ>(), &classInfo<DoubleJ>(),
                &classInfo<CollectionJ>(), &classInfo<SetJ>(), &classInfo<ArrayListJ>(), &classInfo<MapEntryJ>(),
                &classInfo<MapJ>(), &classInfo<LinkedHashMapJ>(),
            };
        }

        Problems startup(JNIEnv *env, Registry &registry, const std::vector<ClassInfo *> &described) {
            Problems problems = RegistryAccess::problems(registry);
            const std::vector<ClassEntry> &classes = RegistryAccess::classes(registry);

            // jnirt's own classes first: the text of every later exception, and the version check, need them.
            // If they are not there, or the versions differ, everything after is noise: stop here.
            const std::vector<ClassInfo *> own = runtimeClasses();
            Problems foundational;
            resolveClasses(env, own, foundational);
            if (foundational.empty()) checkVersion(env, foundational);
            if (!foundational.empty()) {
                problems.insert(problems.end(), foundational.begin(), foundational.end());
                return problems;
            }

            registerNatives(env, runtimeNatives(), problems);

            std::vector<ClassInfo *> user;
            for (ClassInfo *info: described) {
                if (std::find(own.begin(), own.end(), info) == own.end()) user.push_back(info);
            }
            checkAllAdded(user, classes, problems);

            std::vector<ClassInfo *> added;
            for (const ClassEntry &entry: classes) added.push_back(entry.info);
            resolveClasses(env, added, problems);
            registerNatives(env, classes, problems);
            checkExternalMethods(env, classes, problems);
            return problems;
        }
    } // namespace detail

    jint init(JavaVM *vm, std::initializer_list<void (*)(Registry &)> registerFunctions) {
        static std::atomic<bool> called{false};
        if (called.exchange(true)) {
            JNIRT_FATAL("jnirt::init was called twice.\n"
                "  why: JNI_OnLoad runs once per library, and init is the whole of it.\n"
                "  fix: call jnirt::init(vm, {...}) from JNI_OnLoad only, with every register function in one list.");
        }
        setVm(vm);

        detail::Problems problems;
        std::size_t classCount = 0;
        try {
            Registry registry;
            for (auto registerFunction: registerFunctions) registerFunction(registry);
            problems = detail::startup(env(), registry, detail::describedClasses());
            detail::checkDomains(detail::declaredDomains(), problems);
            classCount = detail::RegistryAccess::classes(registry).size();
        } catch (const std::exception &e) {
            problems.push_back(std::string("an exception during startup: ") + e.what());
        }

        if (problems.empty()) {
            // From here on native log lines go to NativeLogBridge, this one included.
            logging::setForwarder(&detail::forwardLogToJava);
            LOGI("startup done: %zu classes", classCount);
            return JNI_VERSION_1_6;
        }
        LOGE("startup failed, %zu problem(s), nothing native will work:", problems.size());
        for (std::size_t i = 0; i < problems.size(); ++i) LOGE("%zu. %s", i + 1, problems[i].c_str());
        LOGE("why: a class or member was renamed, removed or stripped by R8, or the C++ type does not match Kotlin.\n"
             "  fix: compare with `javap -s -p <class>`; keep what native code uses (consumer-rules.pro, "
             "@NativeAccessed).");
        return JNI_ERR;
    }
} // namespace jnirt

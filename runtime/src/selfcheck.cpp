// Startup checks of the Kotlin side that are more than "the member exists": the version, and the `external fun`
// that nobody registered. See docs/adr/0006-class-cache-and-registry.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/class_cache.h>

#include "class_cache_internal.h"
#include "registry_internal.h"

#include <cstring>
#include <set>

#ifndef JNIRT_VERSION
#define JNIRT_VERSION "unknown"
#endif

namespace jnirt {
    namespace {
        // The Kotlin side of jni-runtime (android/.../NativeLoader.kt). It keeps a version string and lists the
        // native methods of a class, which is easier to ask Kotlin than to read through JNI (an array of Method).
        struct NativeLoaderJ : JavaClass<NativeLoaderJ> {
            static constexpr const char *name = "jnirt/NativeLoader";

            static inline StaticField<NativeLoaderJ, jstring> version{"VERSION"};
            static inline StaticMethod<NativeLoaderJ, jstring(jclass)> nativeMethodNames{"nativeMethodNames"};
        };
    } // namespace

    namespace detail {
        ClassInfo &nativeLoaderClassInfo() { return classInfo<NativeLoaderJ>(); }

        void checkVersion(JNIEnv *env, Problems &problems) {
            std::string kotlin = toUtf8(env, NativeLoaderJ::version.get(env).get());
            if (kotlin == JNIRT_VERSION) return;
            problems.push_back("version mismatch: the Kotlin side of jni-runtime is " + kotlin +
                               ", the native side is " JNIRT_VERSION "\n"
                               "  hint: both come from one jni-runtime commit; update the submodule and the Gradle "
                               "module together.");
        }

        void checkExternalMethods(JNIEnv *env, const std::vector<ClassEntry> &classes, Problems &problems) {
            for (const ClassEntry &entry: classes) {
                if (entry.info->cls == nullptr) continue;
                std::string names;
                try {
                    names = toUtf8(env, NativeLoaderJ::nativeMethodNames(env, entry.info->cls).get());
                } catch (const JavaException &e) {
                    problems.push_back("class " + dotted(entry.info->name) +
                                       ": could not list its native methods: " + e.what());
                    continue;
                }
                std::set<std::string> registered;
                for (const NativeEntry &native: entry.natives) registered.insert(native.name);

                for (std::size_t from = 0; from < names.size();) {
                    std::size_t to = names.find(',', from);
                    if (to == std::string::npos) to = names.size();
                    std::string method = names.substr(from, to - from);
                    from = to + 1;
                    if (method.empty() || registered.count(method) != 0) continue;
                    problems.push_back("class " + dotted(entry.info->name) + ": `external fun " + method +
                                       "` has no native function registered\n"
                                       "  hint: add .native<&function>(\"" + method + "\") to r.add<...>() of this class.");
                }
            }
        }

        void checkAllAdded(const std::vector<ClassInfo *> &described, const std::vector<ClassEntry> &classes,
                           Problems &problems) {
            for (const ClassInfo *info: described) {
                if (info->members.empty()) continue;
                bool added = false;
                for (const ClassEntry &entry: classes) added = added || entry.info == info;
                if (added) continue;
                problems.push_back("class " + dotted(info->name) + " has members but is not added to the registry\n"
                                   "  hint: add r.add<...>() for it in a register function.");
            }
        }
    } // namespace detail
} // namespace jnirt

// Registry: collecting the register functions' answers, and registering the natives with the JVM.
// See docs/adr/0006-class-cache-and-registry.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/registry.h>

#include "class_cache_internal.h"
#include "registry_internal.h"

#include <cstring>

namespace jnirt {
    std::size_t Registry::entryFor(detail::ClassInfo &info) {
        for (std::size_t i = 0; i < classes_.size(); ++i) {
            if (classes_[i].info == &info) return i;
        }
        classes_.push_back(detail::ClassEntry{&info, {}});
        return classes_.size() - 1;
    }

    void Registry::addNative(std::size_t classIndex, const detail::NativeEntry &native) {
        detail::ClassEntry &entry = classes_[classIndex];
        for (const detail::NativeEntry &existing: entry.natives) {
            if (std::strcmp(existing.name, native.name) == 0 && std::strcmp(existing.signature, native.signature) == 0) {
                problems_.push_back("native " + detail::dotted(entry.info->name) + "." + native.name + " " +
                                    native.signature + ": registered twice");
                return;
            }
        }
        entry.natives.push_back(native);
    }

    namespace detail {
        namespace {
            jmethodID findMethod(JNIEnv *env, jclass cls, const NativeEntry &native, bool isStatic) {
                jmethodID id = isStatic
                                   ? env->GetStaticMethodID(cls, native.name, native.signature)
                                   : env->GetMethodID(cls, native.name, native.signature);
                return found(env, id) ? id : nullptr;
            }

            void registerNative(JNIEnv *env, const ClassEntry &entry, const NativeEntry &native, Problems &problems) {
                std::string what = "native method " + dotted(entry.info->name) + "." + native.name + " " +
                                   native.signature;
                if (findMethod(env, entry.info->cls, native, native.isStatic) == nullptr) {
                    what += ": not found";
                    if (findMethod(env, entry.info->cls, native, !native.isStatic) != nullptr) {
                        what += native.isStatic
                                    ? "\n  hint: the Kotlin method is an instance method, but the C++ function takes jclass "
                                      "(static). Take jobject, or make the Kotlin method static (@JvmStatic)."
                                    : "\n  hint: the Kotlin method is static, but the C++ function takes jobject. Take jclass, "
                                      "or remove @JvmStatic.";
                    } else {
                        what += "\n  hint: the Kotlin method was renamed or removed, its signature differs, or R8 stripped it.";
                    }
                    problems.push_back(std::move(what));
                    return;
                }
                JNINativeMethod method{const_cast<char *>(native.name), const_cast<char *>(native.signature),
                                       native.function};
                if (env->RegisterNatives(entry.info->cls, &method, 1) != JNI_OK) {
                    env->ExceptionClear();
                    problems.push_back(what + ": RegisterNatives failed\n"
                                       "  hint: the method exists, but it is not native: declare it `external fun`.");
                }
            }
        } // namespace

        void registerNatives(JNIEnv *env, const std::vector<ClassEntry> &classes, Problems &problems) {
            for (const ClassEntry &entry: classes) {
                if (entry.info->cls == nullptr) continue; // not found: already a problem, the natives add only noise
                for (const NativeEntry &native: entry.natives) registerNative(env, entry, native, problems);
            }
        }
    } // namespace detail
} // namespace jnirt

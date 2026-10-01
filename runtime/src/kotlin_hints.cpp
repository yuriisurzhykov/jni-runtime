// Hints for a method that is not found: the Kotlin shapes that change a JVM name (ADR-0008 §9).
//
// Created by Yurii Surzhykov on 10/1/26.

#include "kotlin_hints.h"

#include "class_cache_internal.h"

#include <algorithm>

namespace jnirt::detail {
    namespace {
        bool startsWith(const std::string &text, const std::string &prefix) {
            return text.compare(0, prefix.size(), prefix) == 0;
        }

        // true (and the exception cleared) if the last call threw. -Xcheck:jni also wants this check after a call that
        // went well, before the next JNI call.
        bool threw(JNIEnv *env) {
            if (!env->ExceptionCheck()) return false;
            env->ExceptionClear();
            return true;
        }

        // The names of the methods that `cls` declares, through Class.getDeclaredMethods(). Empty on any failure.
        std::vector<std::string> declaredMethodNames(JNIEnv *env, jclass cls) {
            std::vector<std::string> names;
            LocalRef<jclass> classClass(env, env->FindClass("java/lang/Class"));
            LocalRef<jclass> methodClass(env, env->FindClass("java/lang/reflect/Method"));
            if (threw(env) || !classClass || !methodClass) return names;
            jmethodID getDeclared = env->GetMethodID(classClass.get(), "getDeclaredMethods",
                                                     "()[Ljava/lang/reflect/Method;");
            jmethodID getName = env->GetMethodID(methodClass.get(), "getName", "()Ljava/lang/String;");
            if (threw(env) || getDeclared == nullptr || getName == nullptr) return names;
            LocalRef<jobjectArray> methods(env, static_cast<jobjectArray>(env->CallObjectMethod(cls, getDeclared)));
            if (threw(env) || !methods) return names;
            const jsize count = env->GetArrayLength(methods.get());
            for (jsize i = 0; i < count; ++i) {
                LocalRef<jobject> method(env, env->GetObjectArrayElement(methods.get(), i));
                if (threw(env) || !method) continue;
                LocalRef<jstring> name(env, static_cast<jstring>(env->CallObjectMethod(method.get(), getName)));
                if (threw(env) || !name) continue;
                names.push_back(toUtf8(env, name.get()));
            }
            return names;
        }

        // `Camera$Companion` has the method with this very name and signature: Kotlin put it into the companion object.
        std::string companionHint(JNIEnv *env, const char *jvmClassName, const char *name, const char *descriptor) {
            std::string companion = std::string(jvmClassName) + "$Companion";
            LocalRef<jclass> cls(env, env->FindClass(companion.c_str()));
            if (threw(env) || !cls) return {}; // NoClassDefFoundError: there is no companion
            jmethodID id = env->GetMethodID(cls.get(), name, descriptor);
            if (threw(env) || id == nullptr) return {};
            return "\n  hint: " + dotted(companion.c_str()) + " has " + name + " " + descriptor +
                   ": a function of a companion object is not static. Add @JvmStatic in Kotlin.";
        }
    } // namespace

    std::string mangledNameHint(const std::string &wanted, const std::vector<std::string> &declared) {
        std::string valueClass;
        std::string internal;
        bool hasDefaults = false;
        for (const std::string &name: declared) {
            if (name == wanted + "$default") {
                hasDefaults = true;
            } else if (valueClass.empty() && startsWith(name, wanted + "-")) {
                valueClass = name;
            } else if (internal.empty() && startsWith(name, wanted + "$")) {
                internal = name;
            }
        }
        std::string hint;
        if (!valueClass.empty()) {
            hint += "\n  hint: the class has `" + valueClass + "`: Kotlin gives a function with a value class (or "
                    "UInt, ULong...) in its signature a mangled name. Take the underlying type (Int, Long) on the "
                    "border and wrap it in Kotlin.";
        }
        if (!internal.empty()) {
            hint += "\n  hint: the class has `" + internal + "`: an `internal` function gets the module name in its "
                    "JVM name. Make it `public` or `private`.";
        }
        if (hasDefaults) {
            hint += "\n  hint: the class has `" + wanted + "$default`: the function has default parameter values. The "
                    "JVM method has all the parameters: describe the full signature.";
        }
        return hint;
    }

    std::string hintForMissingMethod(JNIEnv *env, jclass cls, const char *jvmClassName, const char *name,
                                     const char *descriptor) {
        if (env->ExceptionCheck()) env->ExceptionClear();
        std::string hint = mangledNameHint(name, declaredMethodNames(env, cls));
        hint += companionHint(env, jvmClassName, name, descriptor);
        return hint;
    }
} // namespace jnirt::detail

// Runtime-internal part of class_cache, exception, registry and init: the startup steps and their helpers. Read by
// init.cpp and by host tests. Not part of the public API.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_CLASS_CACHE_INTERNAL_H
#define JNIRT_RUNTIME_CLASS_CACHE_INTERNAL_H

#pragma once
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/support/error.h>
#include <jnirt/support/logging/log.h>

#include <string>
#include <vector>

namespace jnirt::detail {
    // One line (or a few) per thing that went wrong at startup. Collected, never thrown: the developer sees all of
    // them from one run.
    using Problems = std::vector<std::string>;

    // "com/acme/Camera$State" -> "com.acme.Camera$State", the form that stack traces and Kotlin use.
    std::string dotted(const char *jvmName);

    // The UTF-8 text of a Java string (empty for nullptr). Modified UTF-8 in the strange cases; this is for logs.
    std::string toUtf8(JNIEnv *env, jstring text);

    // Throwable.toString() without ever throwing. "<a Java exception>" before the Throwable class is resolved.
    std::string describeThrowable(JNIEnv *env, jthrowable throwable);

    // Step 1 and 2 of startup: FindClass for each class, then the lookup of each of its members.
    // A class that is not found is a problem, and its members are not searched.
    void resolveClasses(JNIEnv *env, const std::vector<ClassInfo *> &classes, Problems &problems);

    // Step 3: RegisterNatives for every native of every added class that was found.
    void registerNatives(JNIEnv *env, const std::vector<ClassEntry> &classes, Problems &problems);

    // The ClassInfo of jnirt's own classes whose description is not in runtime_classes.h (exception.cpp,
    // selfcheck.cpp).
    ClassInfo &throwableClassInfo();
    ClassInfo &nativeLoaderClassInfo();

    // jnirt's own classes (Throwable, jnirt.NativeLoader): found before the user's classes, never added to a
    // Registry.
    std::vector<ClassInfo *> runtimeClasses();

    // Step 0, from the declarations of JNIRT_ERROR_DOMAIN: no two enums share a domain name.
    void checkDomains(const std::vector<DomainDeclaration> &declared, Problems &problems);

    // Sends one log line to NativeLogBridge.log. True: Kotlin has it. False: it was not sent (an unattached thread,
    // a pending exception, a line written while one is being forwarded, a receiver that threw) and logcat gets it.
    // This is what init() installs as the forwarder of support/logging.
    bool forwardLogToJava(logging::Level level, const char *tag, const char *message) noexcept;

    // The natives that jnirt registers by itself (NativeLogBridge.setNativeLevel), for startup().
    std::vector<ClassEntry> runtimeNatives();

    // Like jnirt::env(), but nullptr instead of an attach when the thread is not known to the JVM.
    JNIEnv *currentEnvOrNull() noexcept;

    // Kotlin and native versions of jni-runtime are the same.
    void checkVersion(JNIEnv *env, Problems &problems);

    // Every `external fun` of an added class has a registered native.
    void checkExternalMethods(JNIEnv *env, const std::vector<ClassEntry> &classes, Problems &problems);

    // Every class that has members is added to the registry.
    void checkAllAdded(const std::vector<ClassInfo *> &described, const std::vector<ClassEntry> &classes,
                       Problems &problems);

    // The whole startup. `described` is describedClasses() in init, a chosen list in tests. Returns the problems;
    // none means success.
    Problems startup(JNIEnv *env, Registry &registry, const std::vector<ClassInfo *> &described);
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_CLASS_CACHE_INTERNAL_H

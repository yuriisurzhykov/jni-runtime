// The non-template part of class_cache: the list of described classes, the checks before a call, and the startup
// step that finds classes and members. See docs/adr/0006-class-cache-and-registry.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/class_cache.h>
#include <jnirt/support/assert.h>
#include <jnirt/support/error.h>

#include "class_cache_internal.h"
#include "critical.h"

#include <algorithm>
#include <cstring>

namespace jnirt::detail {
    ClassInfo::ClassInfo(const char *jvmName) : name(jvmName) {
        describedClasses().push_back(this);
    }

    std::vector<ClassInfo *> &describedClasses() {
        static auto *list = new std::vector<ClassInfo *>(); // leaked on purpose, like ClassInfo
        return *list;
    }

    Member::Member(ClassInfo &owner, const char *name) : owner_(owner), name_(name) {
        owner.members.push_back(this);
    }

    std::string dotted(const char *jvmName) {
        std::string result(jvmName);
        std::replace(result.begin(), result.end(), '/', '.');
        return result;
    }

    std::string toUtf8(JNIEnv *env, jstring text) {
        if (text == nullptr) return {};
        const char *chars = env->GetStringUTFChars(text, nullptr);
        if (chars == nullptr) return {}; // OutOfMemoryError is pending; the caller's check reports it
        std::string result(chars);
        env->ReleaseStringUTFChars(text, chars);
        return result;
    }

    namespace {
        // "method com.acme.camera.Camera.onFrame (ILjava/lang/String;)V", the form javap -s prints.
        std::string describeMember(const Member &member) {
            std::string text = member.kind();
            text += " " + dotted(member.owner().name);
            if (std::strcmp(member.name(), "<init>") != 0) text += std::string(".") + member.name();
            return text + " " + member.descriptor();
        }

        [[maybe_unused]] void assertNotInsideCritical(const Member &member) {
            JNIRT_ASSERT(!insideCritical(),
                         "%s was called inside withCritical.\n"
                         "  why: between GetPrimitiveArrayCritical and its release no JNI call is allowed; the "
                         "garbage collector is held and a JNI call can deadlock the process.\n"
                         "  fix: call Java before or after withCritical, outside the lambda.",
                         describeMember(member).c_str());
        }

        [[maybe_unused]] void assertResolved(const Member &member) {
            JNIRT_ASSERT(member.resolved(),
                         "%s was used before jnirt::init() found it, or its lookup failed.\n"
                         "  why: a described member is looked up in JNI_OnLoad; before that, and after a failed "
                         "startup, it can not be called.\n"
                         "  fix: call jnirt::init(vm, ...) from JNI_OnLoad and do not use Java from static "
                         "initializers of C++ code.",
                         describeMember(member).c_str());
        }
    } // namespace

    void resultIsNull(const Member &member) {
        // No file and line: the place would be a line of jnirt, not of the caller (entry.cpp leaves it out).
        throw IllegalState(describeMember(member) + ": returned null, but the C++ type of the result has no null.\n"
                           "  fix: declare the result as std::optional<...> (Kotlin: String?).",
                           "", 0);
    }

    void beforeInstanceCall([[maybe_unused]] JNIEnv *env, jobject self, const Member &member) {
        JNIRT_REQUIRE(self != nullptr, "%s: the object is null.", describeMember(member).c_str());
#ifndef NDEBUG
        assertNotInsideCritical(member);
        assertResolved(member);
        JNIRT_ASSERT(env->IsInstanceOf(self, member.owner().cls),
                     "%s: the object is not an instance of %s.\n"
                     "  why: a jobject of another class was passed, JNI would call the method on the wrong object.\n"
                     "  fix: pass the object that belongs to this description.",
                     describeMember(member).c_str(), dotted(member.owner().name).c_str());
#endif
    }

    void beforeStaticCall([[maybe_unused]] const Member &member) {
#ifndef NDEBUG
        assertNotInsideCritical(member);
        assertResolved(member);
#endif
    }

    jclass liveClass(const ClassInfo &info) {
        JNIRT_ASSERT(info.cls != nullptr,
                     "class %s is not resolved.\n"
                     "  why: its global reference is made by jnirt::init(), and it was not called yet, or this "
                     "class is not added to the registry.\n"
                     "  fix: call jnirt::init(vm, {...}) from JNI_OnLoad and add the class with r.add<...>().",
                     dotted(info.name).c_str());
        return info.cls;
    }

    namespace {
        // FindClass found nothing, or its static initializer threw: one problem with the reason.
        void classNotFound(JNIEnv *env, const ClassInfo &info, Problems &problems) {
            LocalRef<jthrowable> pending(env, env->ExceptionOccurred());
            env->ExceptionClear();
            std::string reason = pending ? describeThrowable(env, pending.get()) : "no exception";
            std::string text = "class " + dotted(info.name) + ": not found (" + reason + ")";
            if (reason.find("ExceptionInInitializerError") != std::string::npos) {
                text += "\n  hint: the class exists, but its static initializer (static {} / companion object init)"
                        " threw. FindClass runs it.";
            } else {
                text += "\n  hint: the name is wrong, the class was renamed or removed, or R8 stripped it.";
            }
            problems.push_back(std::move(text));
        }

        void resolveMembers(JNIEnv *env, ClassInfo &info, Problems &problems) {
            for (Member *member: info.members) {
                switch (member->resolve(env, info.cls)) {
                    case Member::Lookup::Found:
                        break;
                    case Member::Lookup::Missing:
                        problems.push_back(describeMember(*member) + ": not found");
                        break;
                    case Member::Lookup::OtherKind:
                        problems.push_back(describeMember(*member) + ": not found\n  hint: " + member->otherKindHint());
                        break;
                }
            }
        }
    } // namespace

    void resolveClasses(JNIEnv *env, const std::vector<ClassInfo *> &classes, Problems &problems) {
        for (ClassInfo *info: classes) {
            if (info->cls == nullptr) {
                LocalRef<jclass> local(env, env->FindClass(info->name));
                if (!local) {
                    classNotFound(env, *info, problems);
                    continue;
                }
                // Never deleted: the class cache lives as long as the process (ADR-0004), so no GlobalRef here.
                info->cls = static_cast<jclass>(env->NewGlobalRef(local.get()));
                if (info->cls == nullptr) JNIRT_FATAL("NewGlobalRef failed for class %s.", dotted(info->name).c_str());
            }
            resolveMembers(env, *info, problems);
        }
    }
} // namespace jnirt::detail

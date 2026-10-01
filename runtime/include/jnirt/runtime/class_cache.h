// Java classes, methods and fields described in C++, found once at startup and called with checked types.
// See docs/adr/0006-class-cache-and-registry.md and docs/learning/class-cache-and-registry-by-example.md.
//
//   struct CameraJ : jnirt::JavaClass<CameraJ> {
//       static constexpr const char *name = "com/acme/camera/Camera";     // slashes, '$' for nested classes
//
//       static inline jnirt::Field<CameraJ, jlong>                    nativeHandle{"nativeHandle"};
//       static inline jnirt::Method<CameraJ, void(jint, jstring)>     onFrame{"onFrame"};
//       static inline jnirt::StaticMethod<CameraJ, jint(jint)>        maxFrames{"maxFrames"};
//       static inline jnirt::Constructor<CameraJ, void(jstring)>      create;
//   };
//
//   jlong handle = CameraJ::nativeHandle.get(env, camera);
//   CameraJ::onFrame(env, camera, index, name.get());                  // a Java exception becomes JavaException
//
// Writing a member is enough to have it looked up: it adds itself to its class when the library is loaded, and
// jnirt::init() (registry.h) finds all of them in JNI_OnLoad. A member that does not exist in Kotlin is a startup
// error with the exact class, name and signature, never a crash at the first call.
//
// A class that has members must be added to the Registry (r.add<CameraJ>()): init checks that none is forgotten.
//
// Types are the JNI types of signature.h. A parameter or result that is one of your classes is written as the
// description (ListenerJ) and passed as a jobject; results that are objects come back as LocalRef<T>.
//
// Debug builds check that the object really is an instance of the class (IsInstanceOf) and that init has run.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_TEST_CLASS_CACHE_H
#define JNI_TEST_CLASS_CACHE_H

#pragma once
#include <jnirt/runtime/detail/jni_calls.h>
#include <jnirt/runtime/exception.h>
#include <jnirt/runtime/ref.h>
#include <jnirt/runtime/signature.h>

#include <type_traits>
#include <vector>

namespace jnirt {
    namespace detail {
        class Member;

        // Everything jnirt knows about one described class. Created the first time a member of the class (or
        // add<C>()) is mentioned, and never destroyed: the global reference to the class lives as long as the
        // process (ADR-0004), and nothing runs at exit that could touch a dead VM.
        struct ClassInfo {
            explicit ClassInfo(const char *jvmName);

            const char *name;                // "com/acme/camera/Camera"
            jclass cls = nullptr;            // a global reference, set by init, never deleted
            std::vector<Member *> members;   // in the order they were created
        };

        // Every class that has been mentioned, in creation order.
        std::vector<ClassInfo *> &describedClasses();

        // The one ClassInfo of C. A function-local static: it exists when the first member of C is constructed,
        // whatever the order of static initialization is.
        template<class C>
        ClassInfo &classInfo() {
            static ClassInfo *info = new ClassInfo(C::name);
            return *info;
        }

        // What is written in C++ for a Java type: the JNI type itself for primitives and JNI references
        // (jint, jstring, ...), jobject for a described class (ListenerJ). Decided without looking inside the
        // type, because a class may name itself in its own members while it is still incomplete.
        template<class T>
        using JniForm = std::conditional_t<std::is_arithmetic_v<T> || std::is_pointer_v<T> || std::is_void_v<T>,
            T, jobject>;

        template<class T>
        constexpr bool isReference = std::is_pointer_v<JniForm<T>>;

        // The C++ type of the JNI call that returns T: jobject for every reference.
        template<class T>
        using CallType = std::conditional_t<isReference<T>, jobject, T>;

        // What a call gives back: void or a primitive as they are, a reference as an owning LocalRef.
        template<class T, bool = isReference<T>>
        struct Returned {
            using type = T;
        };

        template<class T>
        struct Returned<T, true> {
            using type = LocalRef<JniForm<T>>;
        };

        // A Field, Method, ... of a class. Adds itself to the class when constructed.
        class Member {
        public:
            enum class Lookup { Found, Missing, OtherKind };

            Member(const Member &) = delete;
            Member &operator=(const Member &) = delete;

            virtual ~Member() = default;

            const ClassInfo &owner() const noexcept { return owner_; }
            const char *name() const noexcept { return name_; }

            // "method", "static method", "field", "static field", "constructor"
            virtual const char *kind() const = 0;

            // The JVM descriptor or signature: "J", "(ILjava/lang/String;)V".
            virtual const char *descriptor() const = 0;

            // Looks the member up in `cls` and keeps its id. OtherKind: it exists, but as the other kind (static
            // instead of instance).
            virtual Lookup resolve(JNIEnv *env, jclass cls) = 0;

            virtual bool resolved() const noexcept = 0;

            // What to say when the member was not found but exists as the other kind. nullptr: nothing.
            virtual const char *otherKindHint() const noexcept { return nullptr; }

        protected:
            Member(ClassInfo &owner, const char *name);

        private:
            ClassInfo &owner_;
            const char *name_;
        };

        // nullptr from a lookup is a failure: clears the NoSuchMethodError / NoSuchFieldError that came with it.
        template<class Id>
        bool found(JNIEnv *env, Id id) noexcept {
            if (id != nullptr) return true;
            env->ExceptionClear();
            return false;
        }

        // Checks before a call on an object / on the class. Defined in class_cache.cpp.
        void beforeInstanceCall(JNIEnv *env, jobject self, const Member &member);

        void beforeStaticCall(const Member &member);

        // The global reference to the class of `info`, checked to exist in debug builds.
        jclass liveClass(const ClassInfo &info);
    } // namespace detail

    // Base of a Java class description: struct CameraJ : jnirt::JavaClass<CameraJ> { static constexpr const char
    // *name = ...; }. Declared in signature.h, which is all a signature needs; this is the definition.
    template<class Self>
    struct JavaClass {
        // The class itself, a global reference valid for the whole life of the process. Available after init().
        static jclass cls() { return detail::liveClass(detail::classInfo<Self>()); }
    };

    // java.lang.Object, for a parameter or result of Kotlin type Any. It has no members and is never searched for.
    struct Object : JavaClass<Object> {
        static constexpr const char *name = "java/lang/Object";
    };

    // ---- Fields ----

    // A field of an object: CameraJ::nativeHandle.get(env, camera), .set(env, camera, 5).
    // T is a JNI type or a described class; objects come back as LocalRef.
    template<class C, class T>
    class Field final : public detail::Member {
        static_assert(!std::is_void_v<T>, "jnirt: a field can not be void");

    public:
        explicit Field(const char *name) : Member(detail::classInfo<C>(), name) {}

        typename detail::Returned<T>::type get(JNIEnv *env, jobject self) const {
            detail::beforeInstanceCall(env, self, *this);
            if constexpr (detail::isReference<T>) {
                jobject value = detail::getField<jobject>(env, self, id_);
                return LocalRef<detail::JniForm<T> >(env, static_cast<detail::JniForm<T> >(value));
            } else {
                return detail::getField<T>(env, self, id_);
            }
        }

        // For a reference field `value` is a JNI reference (ref.get()); nullptr clears the field.
        void set(JNIEnv *env, jobject self, detail::JniForm<T> value) const {
            detail::beforeInstanceCall(env, self, *this);
            detail::setField<detail::CallType<T> >(env, self, id_, value);
        }

        const char *kind() const override { return "field"; }
        const char *descriptor() const override { return jnirt::descriptor<T>; }

        Lookup resolve(JNIEnv *env, jclass cls) override {
            id_ = env->GetFieldID(cls, name(), descriptor());
            if (detail::found(env, id_)) return Lookup::Found;
            return detail::found(env, env->GetStaticFieldID(cls, name(), descriptor()))
                       ? Lookup::OtherKind
                       : Lookup::Missing;
        }

        bool resolved() const noexcept override { return id_ != nullptr; }

        const char *otherKindHint() const noexcept override {
            return "a static field with this name exists: use StaticField (Kotlin: @JvmField / const val in a companion)";
        }

    private:
        jfieldID id_ = nullptr;
    };

    // A static field: CameraJ::counter.get(env).
    template<class C, class T>
    class StaticField final : public detail::Member {
        static_assert(!std::is_void_v<T>, "jnirt: a field can not be void");

    public:
        explicit StaticField(const char *name) : Member(detail::classInfo<C>(), name) {}

        typename detail::Returned<T>::type get(JNIEnv *env) const {
            detail::beforeStaticCall(*this);
            if constexpr (detail::isReference<T>) {
                jobject value = detail::getStaticField<jobject>(env, owner().cls, id_);
                return LocalRef<detail::JniForm<T> >(env, static_cast<detail::JniForm<T> >(value));
            } else {
                return detail::getStaticField<T>(env, owner().cls, id_);
            }
        }

        void set(JNIEnv *env, detail::JniForm<T> value) const {
            detail::beforeStaticCall(*this);
            detail::setStaticField<detail::CallType<T> >(env, owner().cls, id_, value);
        }

        const char *kind() const override { return "static field"; }
        const char *descriptor() const override { return jnirt::descriptor<T>; }

        Lookup resolve(JNIEnv *env, jclass cls) override {
            id_ = env->GetStaticFieldID(cls, name(), descriptor());
            if (detail::found(env, id_)) return Lookup::Found;
            return detail::found(env, env->GetFieldID(cls, name(), descriptor())) ? Lookup::OtherKind : Lookup::Missing;
        }

        bool resolved() const noexcept override { return id_ != nullptr; }

        const char *otherKindHint() const noexcept override {
            return "an instance field with this name exists: use Field";
        }

    private:
        jfieldID id_ = nullptr;
    };

    // ---- Methods ----

    template<class C, class F>
    class Method {
        static_assert(detail::AlwaysFalse<F>::value,
                      "jnirt: Method<C, F>: F must be a function type, like Method<CameraJ, void(jint, jstring)>");
    };

    // A method of an object: CameraJ::onFrame(env, camera, 1, name.get()).
    // The result is void, a primitive, or a LocalRef for an object. A Java exception is thrown as JavaException.
    template<class C, class R, class... A>
    class Method<C, R(A...)> final : public detail::Member {
    public:
        explicit Method(const char *name) : Member(detail::classInfo<C>(), name) {}

        typename detail::Returned<R>::type operator()(JNIEnv *env, jobject self, detail::JniForm<A>... args) const {
            detail::beforeInstanceCall(env, self, *this);
            if constexpr (std::is_void_v<R>) {
                detail::callMethod<void>(env, self, id_, args...);
                detail::throwIfPending(env);
            } else if constexpr (detail::isReference<R>) {
                jobject result = detail::callMethod<jobject>(env, self, id_, args...);
                detail::throwIfPending(env);
                return LocalRef<detail::JniForm<R> >(env, static_cast<detail::JniForm<R> >(result));
            } else {
                R result = detail::callMethod<R>(env, self, id_, args...);
                detail::throwIfPending(env);
                return result;
            }
        }

        const char *kind() const override { return "method"; }
        const char *descriptor() const override { return jnirt::signature<R(A...)>; }

        Lookup resolve(JNIEnv *env, jclass cls) override {
            id_ = env->GetMethodID(cls, name(), descriptor());
            if (detail::found(env, id_)) return Lookup::Found;
            return detail::found(env, env->GetStaticMethodID(cls, name(), descriptor()))
                       ? Lookup::OtherKind
                       : Lookup::Missing;
        }

        bool resolved() const noexcept override { return id_ != nullptr; }

        const char *otherKindHint() const noexcept override {
            return "a static method with this name and signature exists: use StaticMethod (Kotlin: @JvmStatic)";
        }

    private:
        jmethodID id_ = nullptr;
    };

    template<class C, class F>
    class StaticMethod {
        static_assert(detail::AlwaysFalse<F>::value,
                      "jnirt: StaticMethod<C, F>: F must be a function type, like StaticMethod<CameraJ, jint(jint)>");
    };

    // A static method: CameraJ::maxFrames(env, 4). In Kotlin: @JvmStatic, or a top-level / companion function
    // with @JvmStatic.
    template<class C, class R, class... A>
    class StaticMethod<C, R(A...)> final : public detail::Member {
    public:
        explicit StaticMethod(const char *name) : Member(detail::classInfo<C>(), name) {}

        typename detail::Returned<R>::type operator()(JNIEnv *env, detail::JniForm<A>... args) const {
            detail::beforeStaticCall(*this);
            if constexpr (std::is_void_v<R>) {
                detail::callStaticMethod<void>(env, owner().cls, id_, args...);
                detail::throwIfPending(env);
            } else if constexpr (detail::isReference<R>) {
                jobject result = detail::callStaticMethod<jobject>(env, owner().cls, id_, args...);
                detail::throwIfPending(env);
                return LocalRef<detail::JniForm<R> >(env, static_cast<detail::JniForm<R> >(result));
            } else {
                R result = detail::callStaticMethod<R>(env, owner().cls, id_, args...);
                detail::throwIfPending(env);
                return result;
            }
        }

        const char *kind() const override { return "static method"; }
        const char *descriptor() const override { return jnirt::signature<R(A...)>; }

        Lookup resolve(JNIEnv *env, jclass cls) override {
            id_ = env->GetStaticMethodID(cls, name(), descriptor());
            if (detail::found(env, id_)) return Lookup::Found;
            return detail::found(env, env->GetMethodID(cls, name(), descriptor())) ? Lookup::OtherKind : Lookup::Missing;
        }

        bool resolved() const noexcept override { return id_ != nullptr; }

        const char *otherKindHint() const noexcept override {
            return "an instance method with this name and signature exists: use Method";
        }

    private:
        jmethodID id_ = nullptr;
    };

    template<class C, class F>
    class Constructor {
        static_assert(detail::AlwaysFalse<F>::value,
                      "jnirt: Constructor<C, F>: F must be a function type that returns void, like "
                      "Constructor<CameraJ, void(jstring)>");
    };

    // A constructor: CameraJ::create(env, name.get()) gives a LocalRef<jobject> to the new object.
    // It has no name to write: `static inline jnirt::Constructor<CameraJ, void(jstring)> create;`.
    template<class C, class R, class... A>
    class Constructor<C, R(A...)> final : public detail::Member {
        static_assert(std::is_void_v<R>,
                      "jnirt: a constructor returns void in its signature: Constructor<CameraJ, void(jstring)>");

    public:
        Constructor() : Member(detail::classInfo<C>(), "<init>") {}

        LocalRef<jobject> operator()(JNIEnv *env, detail::JniForm<A>... args) const {
            detail::beforeStaticCall(*this);
            jobject created = env->NewObject(owner().cls, id_, args...);
            detail::throwIfPending(env);
            return LocalRef<jobject>(env, created);
        }

        const char *kind() const override { return "constructor"; }
        const char *descriptor() const override { return jnirt::signature<R(A...)>; }

        Lookup resolve(JNIEnv *env, jclass cls) override {
            id_ = env->GetMethodID(cls, name(), descriptor());
            return detail::found(env, id_) ? Lookup::Found : Lookup::Missing;
        }

        bool resolved() const noexcept override { return id_ != nullptr; }

    private:
        jmethodID id_ = nullptr;
    };
} // namespace jnirt

#endif //JNI_TEST_CLASS_CACHE_H

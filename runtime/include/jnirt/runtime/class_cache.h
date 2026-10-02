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
// Types are the C++ types of convert.h: the JNI types, bool, std::string, std::optional<T>, std::vector<T> and the
// types you add. A parameter or result that is one of your classes is written as the description (ListenerJ) and passed
// as a jobject. Arguments are converted for the call and the temporary Java objects are deleted after it. A result
// that is a raw JNI reference or a described class comes back as a LocalRef<T>; any other result is the C++ value.
//
//   Method<CameraJ, std::string(jint)>  name{"name"};     std::string text = CameraJ::name(env, camera, 3);
//
// Debug builds check that the object really is an instance of the class (IsInstanceOf) and that init has run.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_TEST_CLASS_CACHE_H
#define JNI_TEST_CLASS_CACHE_H

#pragma once
#include <jnirt/runtime/convert.h>
#include <jnirt/runtime/detail/jni_calls.h>
#include <jnirt/runtime/exception.h>
#include <jnirt/runtime/ref.h>
#include <jnirt/runtime/signature.h>

#include <type_traits>
#include <utility>
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

        // A Java method returned null, and the C++ type of its result has no null: IllegalStateException that names
        // the member. Defined in class_cache.cpp.
        [[noreturn]] void resultIsNull(const Member &member);

        // The argument `actual` of a call, as the JNI value for Java type A: a number as it is, or a LocalRef that
        // the caller keeps alive until the call is made (the temporary dies at the end of the call expression).
        // `actual` is converted the way a function argument would be: 1 to jint, "text" to std::string.
        template<class A, class Actual>
        auto marshal(JNIEnv *env, Actual &&actual) {
            ArgOf<A> converted = std::forward<Actual>(actual);
            return RowOf<A>::toJava(env, converted);
        }

        // What a call gives back: `raw` as the C++ result of Java type R. A reference is a new local reference and
        // is owned from here on: it becomes a LocalRef (raw JNI types, described classes) or is deleted after it is
        // converted (everything else).
        template<class R>
        auto makeResult(JNIEnv *env, CallType<R> raw, const Member &member) {
            using Row = RowOf<R>;
            static_assert(!BorrowFlag<Row>::value,
                          "jnirt: this value points into a Java object that may be collected as soon as the "
                          "local reference of the result is gone (a DirectBuffer). Take it as a native parameter, "
                          "or keep the reference yourself and call jnirt::fromJava<DirectBuffer>(env, ref.get()).");
            if constexpr (!std::is_pointer_v<typename Row::Jni>) {
                return Row::fromJava(env, raw);
            } else {
                LocalRef<jobject> owned(env, raw);
                if constexpr (isHandle<R>) {
                    return LocalRef<typename Row::Jni>(env, static_cast<typename Row::Jni>(owned.release()));
                } else {
                    if constexpr (rejectsNull<R>) {
                        if (raw == nullptr) resultIsNull(member);
                    }
                    return Row::fromJava(env, static_cast<typename Row::Jni>(owned.get()));
                }
            }
        }
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
    // T is a type of convert.h or a described class. A raw reference or an object comes back as LocalRef; std::string
    // and the other converted types come back as the C++ value.
    template<class C, class T>
    class Field final : public detail::Member {
        static_assert(!std::is_void_v<T>, "jnirt: a field can not be void");

    public:
        explicit Field(const char *name) : Member(detail::classInfo<C>(), name) {}

        auto get(JNIEnv *env, jobject self) const {
            detail::beforeInstanceCall(env, self, *this);
            detail::CallType<T> raw = detail::getField<detail::CallType<T> >(env, self, id_);
            return detail::makeResult<T>(env, raw, *this);
        }

        // For a raw reference `value` is a JNI reference (ref.get()), nullptr clears the field; for std::string and
        // the like it is the C++ value.
        template<class V>
        void set(JNIEnv *env, jobject self, V &&value) const {
            detail::beforeInstanceCall(env, self, *this);
            auto jni = detail::marshal<T>(env, std::forward<V>(value));
            detail::setField<detail::CallType<T> >(env, self, id_, detail::rawOf(jni));
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

        auto get(JNIEnv *env) const {
            detail::beforeStaticCall(*this);
            detail::CallType<T> raw = detail::getStaticField<detail::CallType<T> >(env, owner().cls, id_);
            return detail::makeResult<T>(env, raw, *this);
        }

        template<class V>
        void set(JNIEnv *env, V &&value) const {
            detail::beforeStaticCall(*this);
            auto jni = detail::marshal<T>(env, std::forward<V>(value));
            detail::setStaticField<detail::CallType<T> >(env, owner().cls, id_, detail::rawOf(jni));
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
    // The result is void, a number, a LocalRef for a raw reference or an object, or the C++ value of a converted type.
    // A Java exception is thrown as JavaException.
    template<class C, class R, class... A>
    class Method<C, R(A...)> final : public detail::Member {
    public:
        explicit Method(const char *name) : Member(detail::classInfo<C>(), name) {}

        template<class... Actual>
        auto operator()(JNIEnv *env, jobject self, Actual &&... args) const {
            static_assert(sizeof...(Actual) == sizeof...(A), "jnirt: wrong number of arguments for this method");
            detail::beforeInstanceCall(env, self, *this);
            if constexpr (std::is_void_v<R>) {
                detail::callMethod<void>(env, self, id_,
                                         detail::rawOf(detail::marshal<A>(env, std::forward<Actual>(args)))...);
                detail::throwIfPending(env);
            } else {
                detail::CallType<R> raw = detail::callMethod<detail::CallType<R> >(
                    env, self, id_, detail::rawOf(detail::marshal<A>(env, std::forward<Actual>(args)))...);
                detail::throwIfPending(env);
                return detail::makeResult<R>(env, raw, *this);
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

        template<class... Actual>
        auto operator()(JNIEnv *env, Actual &&... args) const {
            static_assert(sizeof...(Actual) == sizeof...(A), "jnirt: wrong number of arguments for this method");
            detail::beforeStaticCall(*this);
            if constexpr (std::is_void_v<R>) {
                detail::callStaticMethod<void>(env, owner().cls, id_,
                                               detail::rawOf(detail::marshal<A>(env, std::forward<Actual>(args)))...);
                detail::throwIfPending(env);
            } else {
                detail::CallType<R> raw = detail::callStaticMethod<detail::CallType<R> >(
                    env, owner().cls, id_, detail::rawOf(detail::marshal<A>(env, std::forward<Actual>(args)))...);
                detail::throwIfPending(env);
                return detail::makeResult<R>(env, raw, *this);
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

        template<class... Actual>
        LocalRef<jobject> operator()(JNIEnv *env, Actual &&... args) const {
            static_assert(sizeof...(Actual) == sizeof...(A), "jnirt: wrong number of arguments for this constructor");
            detail::beforeStaticCall(*this);
            jobject created = env->NewObject(owner().cls, id_,
                                             detail::rawOf(detail::marshal<A>(env, std::forward<Actual>(args)))...);
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

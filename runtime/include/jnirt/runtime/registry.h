// Everything JNI_OnLoad does, from descriptions: finds the classes and members of class_cache.h, registers the
// native functions, checks the Kotlin side. See docs/adr/0006-class-cache-and-registry.md and
// docs/learning/class-cache-and-registry-by-example.md.
//
//   bool nativeOpen(JNIEnv *env, jobject thiz, const std::string &url);
//   void nativeSubscribe(JNIEnv *env, jobject thiz, jobject listener);
//
//   void register_camera(jnirt::Registry &r) {
//       r.add<CameraJ>()
//        .native<&nativeOpen>("nativeOpen")                                  // bool(std::string): (String)Boolean
//        .native<&nativeSubscribe, void(ListenerJ)>("nativeSubscribe");      // a jobject needs its Java class
//       r.add<ListenerJ>();                                                  // no natives, but its methods are called
//   }
//
//   JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *) {
//       return jnirt::init(vm, {register_camera, register_other});
//   }
//
// A native function is `R fn(JNIEnv *env, jobject thiz, params...)` for an instance method, or with `jclass` as the
// second parameter for a static one (Kotlin: @JvmStatic). The types are those of convert.h (jint, bool, std::string,
// std::optional<T>, std::vector<T>, your own types). The Java signature is the one of `R(params...)`; the first two
// parameters are never part of it. What is registered is `fn` inside the try/catch of entry.h, with every parameter
// and the result converted: whatever `fn` throws arrives in Kotlin as a Java exception, never as a crash.
//
// init() returns JNI_VERSION_1_6 or JNI_ERR. On JNI_ERR the log holds every problem found, not only the first:
// a class or member that does not exist, a native that Kotlin does not declare (or declares static), an
// `external fun` nobody registered, a described class nobody added, a Kotlin / native version mismatch.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNI_RUNTIME_REGISTRY_H
#define JNI_RUNTIME_REGISTRY_H

#pragma once
#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/convert.h>
#include <jnirt/runtime/entry.h>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

// Here is the definition of the template argument names:
// - Every Fn in this file means - function.
// - Every R means Return type.
// - Every E means Entry.
// - Every P means arguments.
// - Every C means Class.
namespace jnirt {
    class Registry;

    namespace detail {
        struct NativeEntry {
            const char *name;
            const char *signature;
            void *function;
            bool isStatic;
        };

        struct ClassEntry {
            ClassInfo *info;
            std::vector<NativeEntry> natives;
        };

        struct RegistryAccess;

        // native<&fn>: the Java signature is the one of fn.
        struct FromFunction {
        };

        // The pieces of a native function type: R (*)(JNIEnv *, jobject or jclass, P...).
        template<class Fn>
        struct NativeShape {
            static_assert(AlwaysFalse<Fn>::value,
                          "jnirt: native<&fn>: fn must be a plain function: R fn(JNIEnv *env, jobject thiz, "
                          "params...) for an instance method, or with jclass as the second parameter for a "
                          "static one");
        };

        template<class ReturnType, class Self, class... Args>
        struct NativeShape<ReturnType (*)(JNIEnv *, Self, Args...)> {
            static_assert(std::is_same_v<Self, jobject> || std::is_same_v<Self, jclass>,
                          "jnirt: native<&fn>: the second parameter of fn must be exactly jobject (an instance "
                          "method) or jclass (a static method)");

            using Result = ReturnType;
            using Params = std::tuple<Args...>;
            using Signature = ReturnType(Args...); // what Java sees: no JNIEnv*, no this
            static constexpr bool isStatic = std::is_same_v<Self, jclass>;

            // The address that goes to RegisterNatives: Fn inside the try/catch of entry.h, with its parameters and
            // result converted.
            template<auto Func>
            static void *entryPoint() {
                return reinterpret_cast<void *>(&Entry<Func, ReturnType, Self, Args...>::call);
            }
        };

        template<class R, class Self, class... P>
        struct NativeShape<R (*)(JNIEnv *, Self, P...) noexcept> : NativeShape<R (*)(JNIEnv *, Self, P...)> {
        };

        template<class F>
        struct HasJobject;

        template<class R, class... P>
        struct HasJobject<R(P...)>
                : std::bool_constant<std::is_same_v<Plain<R>, jobject> || (std::is_same_v<Plain<P>, jobject> || ...)> {
        };

        template<class Tuple>
        struct PlainTuple;

        template<class... T>
        struct PlainTuple<std::tuple<T...> > {
            using type = std::tuple<Plain<T>...>;
        };

        // The Java signature of a native: given explicitly, and checked against fn, or taken from fn.
        template<class Shape, class Java>
        struct JavaSignature {
            static_assert(AlwaysFalse<Java>::value,
                          "jnirt: native<&fn, J>: J must be a function type with the Java types, like "
                          "void(ListenerJ)");
        };

        template<class Shape>
        struct JavaSignature<Shape, FromFunction> {
            static_assert(!HasJobject<typename Shape::Signature>::value,
                          "jnirt: native<&fn>: fn has a jobject, which does not say which Java class it is. Give "
                          "the Java types: native<&fn, void(ListenerJ)>(\"name\"), with ListenerJ described by "
                          "jnirt::JavaClass (jnirt::Object for Any).");
            using type = typename Shape::Signature;
        };

        template<class Shape, class RJ, class... PJ>
        struct JavaSignature<Shape, RJ(PJ...)> {
            static_assert(sizeof...(PJ) == std::tuple_size_v<typename Shape::Params>,
                          "jnirt: native<&fn, J>: J and fn have a different number of parameters (fn's JNIEnv* and "
                          "jobject / jclass are not counted)");
            static_assert(std::is_same_v<std::tuple<ValueOf<PJ>...>, typename PlainTuple<typename Shape::Params>::type>,
                          "jnirt: native<&fn, J>: a parameter of J does not match fn. A described class is a "
                          "jobject in fn, every other type is the same on both sides");
            static_assert(std::is_same_v<ValueOf<RJ>, Plain<typename Shape::Result> >,
                          "jnirt: native<&fn, J>: the result of J does not match fn. A described class is a "
                          "jobject in fn, every other type is the same on both sides");
            using type = RJ(PJ...);
        };
    } // namespace detail

    template<class C>
    class ClassBuilder;

    // Collects what the register functions say: which classes take part and which natives they have. Nothing
    // here calls JNI; init() does that when all register functions have run.
    class Registry {
    public:
        // The class C takes part: it is looked up with all its members, and its natives are registered. Every class
        // that has members must be added, with or without natives. Adding it again continues the same class.
        template<class C>
        ClassBuilder<C> add();

    private:
        template<class>
        friend class ClassBuilder;
        friend struct detail::RegistryAccess;

        std::size_t entryFor(detail::ClassInfo &info);

        void addNative(std::size_t classIndex, const detail::NativeEntry &native);

        std::vector<detail::ClassEntry> classes_;
        std::vector<std::string> problems_;
    };

    // What add<C>() returns: .native<...>() as many times as the class has natives.
    template<class C>
    class ClassBuilder {
    public:
        // Registers the function Fn as the Java method `name`. `name` must outlive the program: a string literal.
        // The Java signature is the one of Fn; when Fn has a jobject, say which Java classes with `Java`.
        template<auto Fn, class Java = detail::FromFunction>
        ClassBuilder &native(const char *name) {
            using Shape = detail::NativeShape<decltype(Fn)>;
            using JavaFunction = typename detail::JavaSignature<Shape, Java>::type;
            registry_.addNative(index_, detail::NativeEntry{
                                    name, jnirt::signature<JavaFunction>, Shape::template entryPoint<Fn>(),
                                    Shape::isStatic
                                });
            return *this;
        }

    private:
        friend class Registry;

        ClassBuilder(Registry &registry, std::size_t index) : registry_(registry), index_(index) {
        }

        Registry &registry_;
        std::size_t index_;
    };

    template<class C>
    ClassBuilder<C> Registry::add() {
        static_assert(detail::isJavaClass<C>,
                      "jnirt: add<C>(): C must be described as struct C : jnirt::JavaClass<C> { static constexpr "
                      "const char *name = \"com/acme/Camera\"; }");
        return ClassBuilder<C>(*this, entryFor(detail::classInfo<C>()));
    }

    // The whole of JNI_OnLoad: stores the vm, runs the register functions, finds every class and member, registers
    // the natives, checks the Kotlin side. Returns JNI_VERSION_1_6, or JNI_ERR after logging every problem.
    // Call it once, from JNI_OnLoad, with the vm that JNI_OnLoad received. A second call is a crash.
    jint init(JavaVM *vm, std::initializer_list<void (*)(Registry &)> registerFunctions);
} // namespace jnirt

#endif //JNI_RUNTIME_REGISTRY_H

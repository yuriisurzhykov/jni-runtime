// Registry and the startup around it (detail::startup): natives registered with the JVM, and every kind of problem
// the startup reports. jnirt::init() itself is in init_test.cpp (it can be called once per process).

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>

#include "class_cache_internal.h"
#include "registry_internal.h"
#include "test_jvm.h"

#include <algorithm>
#include <string>
#include <type_traits>

using namespace jnirt;

namespace {
    struct ListenerJ : JavaClass<ListenerJ> {
        static constexpr const char *name = "jnirt/test/Listener";

        static inline Method<ListenerJ, void(jint, jstring)> onFrame{"onFrame"};
    };

    // The natives of Camera are called through Java: the Method below is the Java method, the JVM dispatches to
    // whatever was registered.
    struct CameraJ : JavaClass<CameraJ> {
        static constexpr const char *name = "jnirt/test/Camera";

        static inline Constructor<CameraJ, void()> create;
        static inline Field<CameraJ, jint> frames{"frames"};
        static inline Method<CameraJ, jboolean(jstring)> nativeOpen{"nativeOpen"};
        static inline StaticMethod<CameraJ, jint(jint)> nativeStatic{"nativeStatic"};
        static inline Method<CameraJ, void(ListenerJ)> nativeSubscribe{"nativeSubscribe"};
    };

    // ---- The native functions ----

    jboolean nativeOpen(JNIEnv *env, jobject, jstring url) {
        return detail::toUtf8(env, url) == "rtsp://ok" ? JNI_TRUE : JNI_FALSE;
    }

    jint nativeStatic(JNIEnv *, jclass, jint x) { return x * 10; }

    void nativeSubscribe(JNIEnv *env, jobject, jobject listener) { ListenerJ::onFrame(env, listener, 7, nullptr); }

    void forgotten(JNIEnv *, jobject) {
    }

    void staticOnly(JNIEnv *, jclass) {
    }

    void instanceOnly(JNIEnv *, jobject) {
    }

    // wrong on purpose
    void staticTakingJobject(JNIEnv *, jobject) {
    }

    void instanceTakingJclass(JNIEnv *, jclass) {
    }

    jint noexceptNative(JNIEnv *, jclass, jint x) noexcept { return x; }

    // ---- The compile-time rules (the failing cases are recorded in the learning guide) ----

    using detail::FromFunction;
    using detail::JavaSignature;
    using detail::NativeShape;

    using OpenShape = NativeShape<decltype(&nativeOpen)>;
    static_assert(!OpenShape::isStatic);
    static_assert(std::is_same_v<OpenShape::Signature, jboolean(jstring)>);
    static_assert(std::is_same_v<JavaSignature<OpenShape, FromFunction>::type, jboolean(jstring)>);

    using StaticShape = NativeShape<decltype(&nativeStatic)>;
    static_assert(StaticShape::isStatic);
    static_assert(std::is_same_v<StaticShape::Signature, jint(jint)>);

    using NoexceptShape = NativeShape<decltype(&noexceptNative)>;
    static_assert(NoexceptShape::isStatic && std::is_same_v<NoexceptShape::Signature, jint(jint)>);

    using SubscribeShape = NativeShape<decltype(&nativeSubscribe)>;
    static_assert(std::is_same_v<JavaSignature<SubscribeShape, void(ListenerJ)>::type, void(ListenerJ)>);
    static_assert(std::is_same_v<JavaSignature<SubscribeShape, void(Object)>::type, void(Object)>);

    static_assert(std::string_view(jnirt::signature<JavaSignature<SubscribeShape, void(ListenerJ)>::type>) ==
                  "(Ljnirt/test/Listener;)V");

    // ---- helpers ----

    std::string joined(const detail::Problems &problems) {
        std::string all;
        for (const auto &problem: problems) all += problem + "\n";
        return all;
    }

    bool has(const detail::Problems &problems, const std::string &problem) {
        return std::find(problems.begin(), problems.end(), problem) != problems.end();
    }

    // The two classes of this file, as if they were all that is described.
    std::vector<detail::ClassInfo *> described() {
        return {&detail::classInfo<ListenerJ>(), &detail::classInfo<CameraJ>()};
    }

    void registerAllNatives(Registry &r) {
        r.add<CameraJ>()
                .native<&nativeOpen>("nativeOpen")
                .native<&nativeStatic>("nativeStatic")
                .native<&nativeSubscribe, void(ListenerJ)>("nativeSubscribe")
                .native<&forgotten>("forgotten")
                .native<&staticOnly>("staticOnly")
                .native<&instanceOnly>("instanceOnly");
        r.add<ListenerJ>();
    }

    class RegistryTest : public ::testing::Test {
    protected:
        void SetUp() override { env = jnirt::env(); }

        void TearDown() override { EXPECT_FALSE(env->ExceptionCheck()); }

        detail::Problems startup(Registry &registry) const {
            return detail::startup(env, registry, described());
        }

        JNIEnv *env = nullptr;
    };

    // ---- Success ----

    TEST_F(RegistryTest, NativesAreRegisteredAndCalledThroughJava) {
        Registry registry;
        registerAllNatives(registry);
        auto problems = startup(registry);
        ASSERT_TRUE(problems.empty()) << joined(problems);

        auto camera = CameraJ::create(env);
        auto ok = LocalRef<jstring>(env, env->NewStringUTF("rtsp://ok"));
        auto bad = LocalRef<jstring>(env, env->NewStringUTF("http://no"));
        EXPECT_TRUE(CameraJ::nativeOpen(env, camera.get(), ok.get()));
        EXPECT_FALSE(CameraJ::nativeOpen(env, camera.get(), bad.get()));
        EXPECT_EQ(CameraJ::nativeStatic(env, 4), 40);

        CameraJ::nativeSubscribe(env, camera.get(), camera.get()); // the native calls back into Java
        EXPECT_EQ(CameraJ::frames.get(env, camera.get()), 7);
    }

    TEST_F(RegistryTest, AddingAClassAgainContinuesTheSameClass) {
        Registry registry;
        registry.add<CameraJ>().native<&nativeOpen>("nativeOpen");
        registry.add<CameraJ>().native<&nativeStatic>("nativeStatic");
        registry.add<ListenerJ>();
        const auto &classes = detail::RegistryAccess::classes(registry);
        ASSERT_EQ(classes.size(), 2u);
        EXPECT_EQ(classes[0].natives.size(), 2u);
    }

    // ---- Problems ----

    TEST_F(RegistryTest, AnExternalFunNobodyRegisteredIsReported) {
        Registry registry;
        registry.add<CameraJ>()
                .native<&nativeOpen>("nativeOpen")
                .native<&nativeStatic>("nativeStatic")
                .native<&nativeSubscribe, void(ListenerJ)>("nativeSubscribe");
        registry.add<ListenerJ>();
        auto problems = startup(registry);

        for (const char *method: {"forgotten", "staticOnly", "instanceOnly"}) {
            EXPECT_TRUE(has(problems, std::string("class jnirt.test.Camera: `external fun ") + method +
                                      "` has no native function registered\n"
                                      "  hint: add .native<&function>(\"" + method + "\") to r.add<...>() of this class."))
                << joined(problems);
        }
        EXPECT_EQ(problems.size(), 3u) << joined(problems);
    }

    TEST_F(RegistryTest, AStaticInstanceMismatchHasAHint) {
        Registry registry;
        registry.add<CameraJ>()
                .native<&nativeOpen>("nativeOpen")
                .native<&nativeStatic>("nativeStatic")
                .native<&nativeSubscribe, void(ListenerJ)>("nativeSubscribe")
                .native<&forgotten>("forgotten")
                .native<&staticTakingJobject>("staticOnly")      // Kotlin: static
                .native<&instanceTakingJclass>("instanceOnly");  // Kotlin: instance
        registry.add<ListenerJ>();
        auto problems = startup(registry);

        EXPECT_TRUE(has(problems, "native method jnirt.test.Camera.staticOnly ()V: not found\n"
                                  "  hint: the Kotlin method is static, but the C++ function takes jobject. Take jclass, "
                                  "or remove @JvmStatic.")) << joined(problems);
        EXPECT_TRUE(has(problems, "native method jnirt.test.Camera.instanceOnly ()V: not found\n"
                                  "  hint: the Kotlin method is an instance method, but the C++ function takes jclass "
                                  "(static). Take jobject, or make the Kotlin method static (@JvmStatic).")) << joined(problems);
    }

    TEST_F(RegistryTest, ANativeThatKotlinDoesNotHaveIsReported) {
        Registry registry;
        registerAllNatives(registry);
        registry.add<CameraJ>().native<&forgotten>("renamedAway");
        auto problems = startup(registry);
        ASSERT_EQ(problems.size(), 1u) << joined(problems);
        EXPECT_EQ(problems[0], "native method jnirt.test.Camera.renamedAway ()V: not found\n"
                               "  hint: the Kotlin method was renamed or removed, its signature differs, or R8 stripped it.");
    }

    TEST_F(RegistryTest, ANativeForAMethodThatIsNotNativeIsReported) {
        Registry registry;
        registerAllNatives(registry);
        registry.add<CameraJ>().native<&forgotten>("plain");
        auto problems = startup(registry);
        ASSERT_EQ(problems.size(), 1u) << joined(problems);
        EXPECT_NE(problems[0].find("native method jnirt.test.Camera.plain ()V: RegisterNatives failed"), std::string::npos)
            << problems[0];
        EXPECT_NE(problems[0].find("declare it `external fun`"), std::string::npos);
    }

    TEST_F(RegistryTest, TheSameNativeTwiceIsReported) {
        Registry registry;
        registerAllNatives(registry);
        registry.add<CameraJ>().native<&nativeStatic>("nativeStatic");
        auto problems = startup(registry);
        ASSERT_EQ(problems.size(), 1u) << joined(problems);
        EXPECT_EQ(problems[0], "native jnirt.test.Camera.nativeStatic (I)I: registered twice");
    }

    TEST_F(RegistryTest, ADescribedClassThatIsNotAddedIsReported) {
        Registry registry;
        registry.add<CameraJ>()
                .native<&nativeOpen>("nativeOpen")
                .native<&nativeStatic>("nativeStatic")
                .native<&nativeSubscribe, void(ListenerJ)>("nativeSubscribe")
                .native<&forgotten>("forgotten")
                .native<&staticOnly>("staticOnly")
                .native<&instanceOnly>("instanceOnly");
        // ListenerJ has a member, but nobody added it
        auto problems = startup(registry);
        ASSERT_EQ(problems.size(), 1u) << joined(problems);
        EXPECT_EQ(problems[0], "class jnirt.test.Listener has members but is not added to the registry\n"
                               "  hint: add r.add<...>() for it in a register function.");
    }

    TEST_F(RegistryTest, DifferentKotlinAndNativeVersionsStopTheStartup) {
        jclass loader = env->FindClass("jnirt/NativeLoader");
        ASSERT_NE(loader, nullptr);
        jfieldID version = env->GetStaticFieldID(loader, "VERSION", "Ljava/lang/String;");
        auto original = LocalRef<jobject>(env, env->GetStaticObjectField(loader, version));
        auto old = LocalRef<jstring>(env, env->NewStringUTF("0.0.0-old"));
        env->SetStaticObjectField(loader, version, old.get());

        Registry registry;
        registerAllNatives(registry);
        auto problems = startup(registry);

        env->SetStaticObjectField(loader, version, original.get());
        env->DeleteLocalRef(loader);

        ASSERT_EQ(problems.size(), 1u) << joined(problems);
        EXPECT_NE(problems[0].find("version mismatch: the Kotlin side of jni-runtime is 0.0.0-old, the native side is "),
                  std::string::npos) << problems[0];
    }
} // namespace

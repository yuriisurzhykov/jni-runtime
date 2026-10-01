// class_cache (Field, StaticField, Method, StaticMethod, Constructor, JavaException) against a real JVM and the
// Java classes of tests/host/java. The startup step under test is detail::resolveClasses; init() and the registry
// have their own tests.

#include <jnirt/runtime/class_cache.h>
#include <jnirt/runtime/env.h>
#include <jnirt/runtime/exception.h>
#include <jnirt/support/error.h>

#include "class_cache_internal.h"
#include "ref_internal.h"
#include "test_jvm.h"

#include <algorithm>
#include <optional>
#include <string>

using namespace jnirt;

namespace {
    // ---- What the tests describe: tests/host/java/jnirt/test/*.java ----

    struct ListenerJ : JavaClass<ListenerJ> {
        static constexpr const char *name = "jnirt/test/Listener";

        static inline Method<ListenerJ, void(jint, jstring)> onFrame{"onFrame"};
    };

    struct CameraJ : JavaClass<CameraJ> {
        static constexpr const char *name = "jnirt/test/Camera";

        static inline Field<CameraJ, jlong> handle{"handle"};
        static inline Field<CameraJ, jstring> label{"label"};
        static inline Field<CameraJ, ListenerJ> listener{"listener"};
        static inline Field<CameraJ, jint> frames{"frames"};
        static inline StaticField<CameraJ, jint> counter{"counter"};
        static inline StaticField<CameraJ, jstring> title{"title"};

        static inline Method<CameraJ, jint(jint, jint)> add{"add"};
        static inline Method<CameraJ, jstring(jstring)> greet{"greet"};
        static inline Method<CameraJ, CameraJ()> self{"self"}; // a class names itself in its own member
        static inline Method<CameraJ, void(jstring)> fail{"fail"};
        static inline StaticMethod<CameraJ, jint(jint)> twice{"twice"};
        static inline StaticMethod<CameraJ, void()> failStatic{"failStatic"};

        static inline Constructor<CameraJ, void()> create;
        static inline Constructor<CameraJ, void(jstring)> createLabelled;
    };

    struct EchoJ : JavaClass<EchoJ> {
        static constexpr const char *name = "jnirt/test/Echo";

        static inline Field<EchoJ, jboolean> fZ{"fZ"};
        static inline StaticField<EchoJ, jboolean> sfZ{"sfZ"};
        static inline Method<EchoJ, jboolean(jboolean)> echoZ{"echoZ"};
        static inline StaticMethod<EchoJ, jboolean(jboolean)> staticEchoZ{"staticEchoZ"};
        static inline Field<EchoJ, jbyte> fB{"fB"};
        static inline StaticField<EchoJ, jbyte> sfB{"sfB"};
        static inline Method<EchoJ, jbyte(jbyte)> echoB{"echoB"};
        static inline StaticMethod<EchoJ, jbyte(jbyte)> staticEchoB{"staticEchoB"};
        static inline Field<EchoJ, jchar> fC{"fC"};
        static inline StaticField<EchoJ, jchar> sfC{"sfC"};
        static inline Method<EchoJ, jchar(jchar)> echoC{"echoC"};
        static inline StaticMethod<EchoJ, jchar(jchar)> staticEchoC{"staticEchoC"};
        static inline Field<EchoJ, jshort> fS{"fS"};
        static inline StaticField<EchoJ, jshort> sfS{"sfS"};
        static inline Method<EchoJ, jshort(jshort)> echoS{"echoS"};
        static inline StaticMethod<EchoJ, jshort(jshort)> staticEchoS{"staticEchoS"};
        static inline Field<EchoJ, jint> fI{"fI"};
        static inline StaticField<EchoJ, jint> sfI{"sfI"};
        static inline Method<EchoJ, jint(jint)> echoI{"echoI"};
        static inline StaticMethod<EchoJ, jint(jint)> staticEchoI{"staticEchoI"};
        static inline Field<EchoJ, jlong> fJ{"fJ"};
        static inline StaticField<EchoJ, jlong> sfJ{"sfJ"};
        static inline Method<EchoJ, jlong(jlong)> echoJ{"echoJ"};
        static inline StaticMethod<EchoJ, jlong(jlong)> staticEchoJ{"staticEchoJ"};
        static inline Field<EchoJ, jfloat> fF{"fF"};
        static inline StaticField<EchoJ, jfloat> sfF{"sfF"};
        static inline Method<EchoJ, jfloat(jfloat)> echoF{"echoF"};
        static inline StaticMethod<EchoJ, jfloat(jfloat)> staticEchoF{"staticEchoF"};
        static inline Field<EchoJ, jdouble> fD{"fD"};
        static inline StaticField<EchoJ, jdouble> sfD{"sfD"};
        static inline Method<EchoJ, jdouble(jdouble)> echoD{"echoD"};
        static inline StaticMethod<EchoJ, jdouble(jdouble)> staticEchoD{"staticEchoD"};
        static inline Constructor<EchoJ, void()> create;
    };

    // ---- Descriptions that do not match Kotlin, for the error messages ----

    struct BrokenCameraJ : JavaClass<BrokenCameraJ> {
        static constexpr const char *name = "jnirt/test/Camera";

        static inline Method<BrokenCameraJ, jint(jint)> missing{"nope"};
        static inline Method<BrokenCameraJ, jint(jint)> twiceIsStatic{"twice"};
        static inline StaticMethod<BrokenCameraJ, jint(jint, jint)> addIsInstance{"add"};
        static inline Method<BrokenCameraJ, jint(jint)> wrongSignature{"add"};
        static inline Field<BrokenCameraJ, jint> wrongType{"handle"};
        static inline StaticField<BrokenCameraJ, jint> framesIsInstance{"frames"};
        static inline Field<BrokenCameraJ, jint> counterIsStatic{"counter"};
        static inline Constructor<BrokenCameraJ, void(jint)> noSuchConstructor;
    };

    struct MissingClassJ : JavaClass<MissingClassJ> {
        static constexpr const char *name = "jnirt/test/Nope";

        static inline Method<MissingClassJ, void()> never{"never"};
    };

    struct BoomJ : JavaClass<BoomJ> {
        static constexpr const char *name = "jnirt/test/Boom";

        static inline StaticMethod<BoomJ, void()> touch{"touch"};
    };

    // ---- helpers ----

    std::string joined(const detail::Problems &problems) {
        std::string all;
        for (const auto &problem: problems) all += problem + "\n";
        return all;
    }

    bool has(const detail::Problems &problems, const std::string &problem) {
        return std::find(problems.begin(), problems.end(), problem) != problems.end();
    }

    bool hasPrefix(const detail::Problems &problems, const std::string &prefix) {
        return std::any_of(problems.begin(), problems.end(),
                           [&](const std::string &p) { return p.compare(0, prefix.size(), prefix) == 0; });
    }

    // A fixture that finds the described classes once. Every test starts and ends with no live references made
    // through jnirt (the counters exist in debug builds only).
    class ClassCache : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            GTEST_FLAG_SET(death_test_style, "threadsafe");
            JNIEnv *env = jnirt::env();
            detail::Problems problems;
            detail::resolveClasses(env, {&detail::throwableClassInfo()}, problems);
            detail::resolveClasses(env, {
                                       &detail::classInfo<ListenerJ>(), &detail::classInfo<CameraJ>(),
                                       &detail::classInfo<EchoJ>()
                                   }, problems);
            ASSERT_TRUE(problems.empty()) << joined(problems);
        }

        void SetUp() override {
            env = jnirt::env();
            localsAtStart = detail::liveLocalRefsOnThisThread();
            globalsAtStart = detail::liveGlobalRefs();
        }

        void TearDown() override {
            EXPECT_EQ(detail::liveLocalRefsOnThisThread(), localsAtStart) << "a LocalRef leaked out of the test";
            EXPECT_EQ(detail::liveGlobalRefs(), globalsAtStart) << "a GlobalRef leaked";
            EXPECT_FALSE(env->ExceptionCheck());
        }

        LocalRef<jstring> newString(const char *text) const {
            return LocalRef<jstring>(env, env->NewStringUTF(text));
        }

        LocalRef<jobject> newCamera() const { return CameraJ::create(env); }

        JNIEnv *env = nullptr;
        int localsAtStart = 0;
        std::size_t globalsAtStart = 0;
    };

    // The same checks for every primitive: set / get a field, call a method, on an object and on the class.
    template<class T, class F, class SF, class M, class SM>
    void roundTrip(JNIEnv *env, jobject echo, T value, const F &field, const SF &staticField, const M &method,
                   const SM &staticMethod) {
        field.set(env, echo, value);
        EXPECT_EQ(field.get(env, echo), value);
        staticField.set(env, value);
        EXPECT_EQ(staticField.get(env), value);
        EXPECT_EQ(method(env, echo, value), value);
        EXPECT_EQ(staticMethod(env, value), value);
    }

    // ---- Calls ----

    TEST_F(ClassCache, EveryPrimitiveKindRoundTrips) {
        auto echo = EchoJ::create(env);
        roundTrip<jboolean>(env, echo.get(), JNI_TRUE, EchoJ::fZ, EchoJ::sfZ, EchoJ::echoZ, EchoJ::staticEchoZ);
        roundTrip<jbyte>(env, echo.get(), 7, EchoJ::fB, EchoJ::sfB, EchoJ::echoB, EchoJ::staticEchoB);
        roundTrip<jchar>(env, echo.get(), u'x', EchoJ::fC, EchoJ::sfC, EchoJ::echoC, EchoJ::staticEchoC);
        roundTrip<jshort>(env, echo.get(), 300, EchoJ::fS, EchoJ::sfS, EchoJ::echoS, EchoJ::staticEchoS);
        roundTrip<jint>(env, echo.get(), 123456, EchoJ::fI, EchoJ::sfI, EchoJ::echoI, EchoJ::staticEchoI);
        roundTrip<jlong>(env, echo.get(), 1234567890123LL, EchoJ::fJ, EchoJ::sfJ, EchoJ::echoJ, EchoJ::staticEchoJ);
        roundTrip<jfloat>(env, echo.get(), 1.5f, EchoJ::fF, EchoJ::sfF, EchoJ::echoF, EchoJ::staticEchoF);
        roundTrip<jdouble>(env, echo.get(), 2.25, EchoJ::fD, EchoJ::sfD, EchoJ::echoD, EchoJ::staticEchoD);
    }

    TEST_F(ClassCache, PrimitiveFieldsAndMethodsOfAnObject) {
        auto camera = newCamera();
        CameraJ::handle.set(env, camera.get(), 42);
        EXPECT_EQ(CameraJ::handle.get(env, camera.get()), 42);
        EXPECT_EQ(CameraJ::add(env, camera.get(), 2, 3), 5);
        EXPECT_EQ(CameraJ::twice(env, 21), 42);
    }

    TEST_F(ClassCache, StaticFieldOfAClass) {
        CameraJ::counter.set(env, 7);
        EXPECT_EQ(CameraJ::counter.get(env), 7);
        auto title = CameraJ::title.get(env);
        EXPECT_EQ(detail::toUtf8(env, title.get()), "camera");
    }

    TEST_F(ClassCache, ObjectResultsAreLocalRefs) {
        auto camera = newCamera();
        auto name = newString("world");
        auto greeting = CameraJ::greet(env, camera.get(), name.get());
        EXPECT_EQ(detail::toUtf8(env, greeting.get()), "hello world");

        auto same = CameraJ::self(env, camera.get()); // a method that returns its own class
        EXPECT_TRUE(env->IsSameObject(same.get(), camera.get()));
    }

    TEST_F(ClassCache, ObjectFields) {
        auto camera = newCamera();
        EXPECT_FALSE(CameraJ::label.get(env, camera.get())); // null field: an empty LocalRef

        auto text = newString("front");
        CameraJ::label.set(env, camera.get(), text.get());
        EXPECT_EQ(detail::toUtf8(env, CameraJ::label.get(env, camera.get()).get()), "front");

        CameraJ::listener.set(env, camera.get(), camera.get()); // a field typed with a described class
        auto listener = CameraJ::listener.get(env, camera.get());
        EXPECT_TRUE(env->IsSameObject(listener.get(), camera.get()));

        CameraJ::label.set(env, camera.get(), nullptr);
        EXPECT_FALSE(CameraJ::label.get(env, camera.get()));
    }

    TEST_F(ClassCache, ConstructorsMakeObjects) {
        auto name = newString("rear");
        auto camera = CameraJ::createLabelled(env, name.get());
        ASSERT_TRUE(camera);
        EXPECT_EQ(detail::toUtf8(env, CameraJ::label.get(env, camera.get()).get()), "rear");
    }

    TEST_F(ClassCache, AnInterfaceMethodIsCalledOnAnImplementation) {
        auto camera = newCamera();
        auto name = newString("frame-1");
        ListenerJ::onFrame(env, camera.get(), 5, name.get());
        EXPECT_EQ(CameraJ::frames.get(env, camera.get()), 5);
    }

    TEST_F(ClassCache, TheClassItselfIsAvailable) {
        auto camera = newCamera();
        ASSERT_NE(CameraJ::cls(), nullptr);
        EXPECT_TRUE(env->IsInstanceOf(camera.get(), CameraJ::cls()));
    }

    // ---- Java exceptions ----

    TEST_F(ClassCache, AJavaExceptionBecomesJavaException) {
        auto camera = newCamera();
        auto message = newString("boom");
        try {
            CameraJ::fail(env, camera.get(), message.get());
            FAIL() << "no exception";
        } catch (const JavaException &e) {
            EXPECT_STREQ(e.what(), "java.lang.IllegalStateException: boom");
            EXPECT_FALSE(env->ExceptionCheck()) << "the pending exception must be cleared";
            auto stateClass = LocalRef<jclass>(env, env->FindClass("java/lang/IllegalStateException"));
            EXPECT_TRUE(env->IsInstanceOf(e.throwable(), stateClass.get()));
        }
    }

    TEST_F(ClassCache, JavaExceptionFromAStaticMethodAndAConstructor) {
        try {
            CameraJ::failStatic(env);
            FAIL() << "no exception";
        } catch (const JavaException &e) {
            EXPECT_STREQ(e.what(), "java.lang.UnsupportedOperationException: static boom");
        }
        try {
            CameraJ::createLabelled(env, nullptr);
            FAIL() << "no exception";
        } catch (const JavaException &e) {
            EXPECT_STREQ(e.what(), "java.lang.IllegalArgumentException: label is null");
        }
    }

    TEST_F(ClassCache, ACopyOfJavaExceptionKeepsTheThrowable) {
#ifdef NDEBUG
        GTEST_SKIP() << "the reference counters exist in debug builds only";
#endif
        auto camera = newCamera();
        auto message = newString("copy");
        std::size_t globalsBefore = detail::liveGlobalRefs();
        {
            std::optional<JavaException> kept;
            try {
                CameraJ::fail(env, camera.get(), message.get());
            } catch (const JavaException &e) {
                kept = e; // the original is destroyed when the catch ends
            }
            ASSERT_TRUE(kept.has_value());
            const JavaException &copy = *kept;
            EXPECT_STREQ(copy.what(), "java.lang.IllegalStateException: copy");
            EXPECT_NE(copy.throwable(), nullptr);
            EXPECT_EQ(detail::liveGlobalRefs(), globalsBefore + 1) << "copies share one reference";
        }
        EXPECT_EQ(detail::liveGlobalRefs(), globalsBefore) << "the reference is released with the last copy";
    }

    // ---- Misuse ----

    TEST_F(ClassCache, ANullObjectIsAnError) {
        EXPECT_THROW(CameraJ::add(env, nullptr, 1, 2), IllegalArgument);
    }

    TEST_F(ClassCache, AnObjectOfAnotherClassIsCaughtInDebug) {
#ifdef NDEBUG
        GTEST_SKIP() << "the IsInstanceOf check exists in debug builds only";
#else
        auto text = newString("not a camera");
        EXPECT_DEATH(CameraJ::add(env, text.get(), 1, 2), "the object is not an instance of jnirt.test.Camera");
#endif
    }

    // ---- Startup problems: what the developer reads ----

    TEST_F(ClassCache, ProblemsAreCollectedAndNameWhatIsWrong) {
        detail::Problems problems;
        detail::resolveClasses(env, {&detail::classInfo<BrokenCameraJ>()}, problems);

        EXPECT_TRUE(has(problems, "method jnirt.test.Camera.nope (I)I: not found")) << joined(problems);
        EXPECT_TRUE(has(problems, "method jnirt.test.Camera.twice (I)I: not found\n"
                                  "  hint: a static method with this name and signature exists: use StaticMethod "
                                  "(Kotlin: @JvmStatic)")) << joined(problems);
        EXPECT_TRUE(has(problems, "static method jnirt.test.Camera.add (II)I: not found\n"
                                  "  hint: an instance method with this name and signature exists: use Method"))
            << joined(problems);
        EXPECT_TRUE(has(problems, "method jnirt.test.Camera.add (I)I: not found")) << joined(problems);
        EXPECT_TRUE(has(problems, "field jnirt.test.Camera.handle I: not found")) << joined(problems);
        EXPECT_TRUE(hasPrefix(problems, "static field jnirt.test.Camera.frames I: not found\n  hint: ")) << joined(problems);
        EXPECT_TRUE(hasPrefix(problems, "field jnirt.test.Camera.counter I: not found\n  hint: ")) << joined(problems);
        EXPECT_TRUE(has(problems, "constructor jnirt.test.Camera (I)V: not found")) << joined(problems);
        EXPECT_EQ(problems.size(), 8u) << "all problems at once, not the first only\n" << joined(problems);
    }

    TEST_F(ClassCache, AMissingClassIsOneProblemAndItsMembersAreNotSearched) {
        detail::Problems problems;
        detail::resolveClasses(env, {&detail::classInfo<MissingClassJ>()}, problems);
        ASSERT_EQ(problems.size(), 1u) << joined(problems);
        EXPECT_TRUE(hasPrefix(problems, "class jnirt.test.Nope: not found (java.lang.NoClassDefFoundError")) << joined(problems);
        EXPECT_NE(problems[0].find("hint: the name is wrong"), std::string::npos);
    }

    TEST_F(ClassCache, AFailingStaticInitializerIsExplained) {
        detail::Problems problems;
        detail::resolveClasses(env, {&detail::classInfo<BoomJ>()}, problems);
        ASSERT_EQ(problems.size(), 1u) << joined(problems);
        EXPECT_NE(problems[0].find("java.lang.ExceptionInInitializerError, caused by "
                                   "java.lang.IllegalStateException: no context"), std::string::npos) << problems[0];
        EXPECT_NE(problems[0].find("its static initializer"), std::string::npos) << problems[0];
    }
} // namespace

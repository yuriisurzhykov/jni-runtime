// entry(): every native function is registered inside a try/catch, and each kind of C++ exception arrives in Java as
// the exception ADR-0002 §5 promises. The natives of tests/host/java/jnirt/test/Thrower.java throw on purpose;
// Thrower.probe*() turns whatever Java receives into one line of text.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/support/assert.h>
#include <jnirt/support/error.h>

#include "class_cache_internal.h"
#include "ref_internal.h"
#include "registry_internal.h"
#include "test_jvm.h"
#include "utf.h"

#include <new>
#include <stdexcept>
#include <string>

namespace entrytest {
    enum class Errc { AuthFailed = 7 };
}
JNIRT_ERROR_DOMAIN(entrytest::Errc, "entrytest")

using namespace jnirt;

namespace {
    struct ThrowerJ : JavaClass<ThrowerJ> {
        static constexpr const char *name = "jnirt/test/Thrower";

        static inline Constructor<ThrowerJ, void()> create;
        static inline Method<ThrowerJ, void()> failing{"failing"};
        static inline Method<ThrowerJ, jboolean()> identity{"identity"};
        static inline StaticMethod<ThrowerJ, jstring(jint)> probe{"probe"};
        static inline StaticMethod<ThrowerJ, jstring(jint)> probeStatic{"probeStatic"};
        static inline StaticMethod<ThrowerJ, jstring(jint)> probeObject{"probeObject"};
        static inline StaticMethod<ThrowerJ, jstring()> probeRawPending{"probeRawPending"};
        static inline StaticMethod<ThrowerJ, jint(jint)> depth{"depth"};
    };

    // ---- The natives: each `kind` throws something else ----

    [[noreturn]] void nest(int level) {
        if (level == 0) throw jnirt::Error(jnirt::Errc::Internal, "level 0");
        try {
            nest(level - 1);
        } catch (...) {
            std::throw_with_nested(jnirt::Error(jnirt::Errc::Internal, "level %d", level));
        }
    }

    void throwKind(JNIEnv *env, jobject self, jint kind) {
        switch (kind) {
            case 0: JNIRT_REQUIRE(false, "timeoutMs must be positive, got %d", -1);
            case 1: JNIRT_CHECK(false, "grab() before connect()");
            case 2: throw jnirt::Error(entrytest::Errc::AuthFailed, "camera %s rejected login", "10.0.0.5");
            case 3:
                try {
                    throw std::runtime_error("disk full");
                } catch (...) {
                    std::throw_with_nested(jnirt::Error(jnirt::Errc::Io, "write failed"));
                }
            case 4: ThrowerJ::failing(env, self); // a Java exception, not caught here
                return;
            case 5:
                try {
                    ThrowerJ::failing(env, self);
                } catch (const JavaException &) {
                    std::throw_with_nested(jnirt::Error(jnirt::Errc::Io, "connect failed"));
                }
                return;
            case 6: throw std::bad_alloc();
            case 7: throw std::runtime_error("disk full");
            case 8: throw 42;
            case 9: throw jnirt::Error(jnirt::Errc::NotFound, "камера \xF0\x9F\x98\x80 \xFF не найдена");
            case 10: nest(9); // ten exceptions in one chain
            default: return;
        }
    }

    void boom(JNIEnv *env, jobject self, jint kind) { throwKind(env, self, kind); }

    jint staticBoom(JNIEnv *env, jclass, jint kind) {
        throwKind(env, nullptr, kind);
        return 5;
    }

    jint add(JNIEnv *, jobject, jint a, jint b) noexcept { return a + b; }

    jstring greeting(JNIEnv *env, jobject self, jint kind) {
        throwKind(env, self, kind);
        return env->NewStringUTF("hello");
    }

    void callFailing(JNIEnv *env, jobject self) { ThrowerJ::failing(env, self); }

    void rawPending(JNIEnv *env, jobject) {
        jclass cls = env->FindClass("java/lang/UnsupportedOperationException");
        env->ThrowNew(cls, "raw");
        env->DeleteLocalRef(cls);
        throw std::runtime_error("this one is dropped");
    }

    void registerThrower(Registry &r) {
        r.add<ThrowerJ>()
                .native<&boom>("boom")
                .native<&staticBoom>("staticBoom")
                .native<&add>("add")
                .native<&greeting>("greeting")
                .native<&callFailing>("callFailing")
                .native<&rawPending>("rawPending");
    }

    // ---- helpers ----

    std::string text(JNIEnv *env, jstring java) { return detail::toUtf8(env, java); }

    bool contains(const std::string &whole, const std::string &part) { return whole.find(part) != std::string::npos; }

    std::u16string utf16(JNIEnv *env, jstring java) {
        const jchar *chars = env->GetStringChars(java, nullptr);
        std::u16string result(reinterpret_cast<const char16_t *>(chars), static_cast<std::size_t>(env->GetStringLength(java)));
        env->ReleaseStringChars(java, chars);
        return result;
    }

    class EntryTest : public ::testing::Test {
    protected:
        void SetUp() override {
            env = jnirt::env();
            Registry registry;
            registerThrower(registry);
            auto problems = detail::startup(env, registry, {&detail::classInfo<ThrowerJ>()});
            ASSERT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0]);
        }

        void TearDown() override { EXPECT_FALSE(env->ExceptionCheck()); }

        std::string probe(jint kind) { return text(env, ThrowerJ::probe(env, kind).get()); }

        JNIEnv *env = nullptr;
    };

    // ---- Programmer errors ----

    TEST_F(EntryTest, ARequireBecomesIllegalArgumentExceptionWithTheFileAndLine) {
        std::string line = probe(0);
        EXPECT_TRUE(line.rfind("java.lang.IllegalArgumentException: timeoutMs must be positive, got -1 (entry_test.cpp:", 0) == 0)
            << line;
        EXPECT_EQ(line.back(), ')');
    }

    TEST_F(EntryTest, ACheckBecomesIllegalStateException) {
        std::string line = probe(1);
        EXPECT_TRUE(line.rfind("java.lang.IllegalStateException: grab() before connect() (entry_test.cpp:", 0) == 0) << line;
    }

    // ---- Recoverable errors ----

    TEST_F(EntryTest, AnErrorBecomesNativeExceptionWithDomainAndCode) {
        EXPECT_EQ(probe(2), "jnirt.NativeException[entrytest/7]: camera 10.0.0.5 rejected login");
    }

    TEST_F(EntryTest, ANestedStdExceptionBecomesTheCause) {
        EXPECT_EQ(probe(3), "jnirt.NativeException[jnirt/3]: write failed <- "
                            "jnirt.NativeException[jnirt/4]: std::runtime_error: disk full");
    }

    TEST_F(EntryTest, ANestedJavaExceptionKeepsTheOriginalThrowableAsTheCause) {
        EXPECT_EQ(probe(5), "jnirt.NativeException[jnirt/3]: connect failed <- "
                            "java.lang.IllegalStateException: original");
    }

    TEST_F(EntryTest, ALongChainIsCutAtEightExceptions) {
        EXPECT_EQ(ThrowerJ::depth(env, 10), 8);
        EXPECT_EQ(ThrowerJ::depth(env, 3), 2);
    }

    // ---- Java exceptions pass through ----

    TEST_F(EntryTest, AJavaExceptionArrivesAsTheSameObject) {
        auto thrower = ThrowerJ::create(env);
        EXPECT_TRUE(ThrowerJ::identity(env, thrower.get()));
        EXPECT_EQ(probe(4), "java.lang.IllegalStateException: original");
    }

    // ---- The rest ----

    TEST_F(EntryTest, BadAllocBecomesOutOfMemoryError) {
        EXPECT_EQ(probe(6), "java.lang.OutOfMemoryError: native code ran out of memory");
    }

    TEST_F(EntryTest, AnyOtherStdExceptionBecomesAnInternalNativeException) {
        EXPECT_EQ(probe(7), "jnirt.NativeException[jnirt/4]: std::runtime_error: disk full");
    }

    TEST_F(EntryTest, SomethingThatIsNotAnExceptionClassIsStillCaught) {
        EXPECT_EQ(probe(8), "jnirt.NativeException[jnirt/4]: unknown C++ exception");
    }

    TEST_F(EntryTest, TextIsConvertedFromUtf8WithoutTouchingTheJvm) {
        auto line = ThrowerJ::probe(env, 9);
        EXPECT_EQ(utf16(env, line.get()),
                  u"jnirt.NativeException[jnirt/2]: камера \U0001F600 � не найдена");
    }

    TEST_F(EntryTest, StaticAndObjectReturningNativesAreWrappedToo) {
        EXPECT_EQ(text(env, ThrowerJ::probeStatic(env, 2).get()),
                  "jnirt.NativeException[entrytest/7]: camera 10.0.0.5 rejected login");
        EXPECT_EQ(text(env, ThrowerJ::probeObject(env, 2).get()),
                  "jnirt.NativeException[entrytest/7]: camera 10.0.0.5 rejected login");
        EXPECT_EQ(text(env, ThrowerJ::probeObject(env, 99).get()), "hello");
    }

    TEST_F(EntryTest, ANativeThatDoesNotThrowIsUntouched) {
        EXPECT_EQ(probe(99), "no exception");
        EXPECT_EQ(ThrowerJ::depth(env, 99), 0);
    }

    TEST_F(EntryTest, APendingJavaExceptionWinsAndTheCppOneIsLogged) {
        testing::internal::CaptureStderr();
        std::string line = text(env, ThrowerJ::probeRawPending(env).get());
        std::string log = testing::internal::GetCapturedStderr();
        EXPECT_EQ(line, "java.lang.UnsupportedOperationException: raw");
        EXPECT_TRUE(contains(log, "a Java exception is already pending: std::runtime_error: this one is dropped")) << log;
    }

    // ---- Logging and bookkeeping ----

    TEST_F(EntryTest, ProgrammerAndRecoverableErrorsAreLoggedAsWarningsOthersAsErrors) {
        testing::internal::CaptureStderr();
        probe(0);
        probe(2);
        probe(7);
        probe(4);
        std::string log = testing::internal::GetCapturedStderr();
        EXPECT_TRUE(contains(log, "W/jnirt: IllegalArgumentException: timeoutMs must be positive, got -1 (entry_test.cpp:")) << log;
        EXPECT_TRUE(contains(log, "W/jnirt: NativeException entrytest/7: camera 10.0.0.5 rejected login")) << log;
        EXPECT_TRUE(contains(log, "E/jnirt: unexpected C++ exception std::runtime_error: disk full")) << log;
        // a Java exception passing through is not our error: it is not logged
        EXPECT_FALSE(contains(log, "original")) << log;
    }

    TEST_F(EntryTest, NoReferenceIsLeftBehind) {
        const std::size_t globals = detail::liveGlobalRefs();
        const int locals = detail::liveLocalRefsOnThisThread();
        testing::internal::CaptureStderr();
        for (jint kind = 0; kind <= 10; ++kind) probe(kind);
        auto thrower = ThrowerJ::create(env);
        ThrowerJ::identity(env, thrower.get());
        testing::internal::GetCapturedStderr();
        thrower.reset();
        EXPECT_EQ(detail::liveGlobalRefs(), globals);
        EXPECT_EQ(detail::liveLocalRefsOnThisThread(), locals);
    }

    TEST_F(EntryTest, ANativeThatReturnsNormallyReturnsItsValue) {
        auto thrower = ThrowerJ::create(env);
        // add is registered with noexcept: the wrapper handles that signature too.
        jclass cls = env->GetObjectClass(thrower.get());
        jmethodID id = env->GetMethodID(cls, "add", "(II)I");
        EXPECT_EQ(env->CallIntMethod(thrower.get(), id, 2, 3), 5);
        env->DeleteLocalRef(cls);
    }

    // ---- UTF-8 to UTF-16 ----

    std::u16string convert(const char *bytes) { return detail::utf8ToUtf16(bytes); }

    TEST(Utf8, AsciiCyrillicAndSupplementaryCharacters) {
        EXPECT_EQ(convert("abc"), u"abc");
        EXPECT_EQ(convert("привет"), u"привет");
        EXPECT_EQ(convert("\xF0\x9F\x98\x80"), u"\U0001F600"); // a surrogate pair in UTF-16
        EXPECT_EQ(convert(""), u"");
    }

    TEST(Utf8, EveryBadSequenceBecomesOneReplacementCharacter) {
        EXPECT_EQ(convert("a\xFF" "b"), u"a�" "b");                 // never a lead byte
        EXPECT_EQ(convert("a\x80" "b"), u"a�" "b");                 // a stray continuation byte
        EXPECT_EQ(convert("a\xD0"), u"a�");                         // cut off at the end
        EXPECT_EQ(convert("a\xE2\x82" "b"), u"a�" "b");             // cut off, then good text
        EXPECT_EQ(convert("\xC0\x80"), u"��");                 // overlong NUL: both bytes are bad
        EXPECT_EQ(convert("\xED\xA0\x80"), u"���");       // a UTF-16 surrogate written as UTF-8
        EXPECT_EQ(convert("\xF4\x90\x80\x80"), u"����"); // above U+10FFFF
    }
} // namespace

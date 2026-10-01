// Native log lines to Kotlin: detail::forwardLogToJava installed as the forwarder of support/logging, against the
// stand-in jnirt.NativeLogBridge of tests/host/java (it collects the lines instead of calling Timber).
// jnirt::init() installing the forwarder is covered in init_test.cpp.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/support/logging/macros.h>

#include "class_cache_internal.h"
#include "test_jvm.h"

#include <string>
#include <thread>

LOG_DEFINE_TAG("logtest")

using namespace jnirt;

namespace {
    struct LogSourceJ : JavaClass<LogSourceJ> {
        static constexpr const char *name = "jnirt/test/LogSource";

        static inline StaticMethod<LogSourceJ, void()> reset{"reset"};
        static inline StaticMethod<LogSourceJ, jstring()> received{"received"};
        static inline StaticMethod<LogSourceJ, void(jboolean)> receiverThrows{"receiverThrows"};
        static inline StaticMethod<LogSourceJ, void(jstring)> logInsideReceiver{"logInsideReceiver"};
        static inline StaticMethod<LogSourceJ, void(jint)> setLevel{"setLevel"};
        static inline StaticMethod<LogSourceJ, jstring(jstring)> probePending{"probePending"};
    };

    void logLine(JNIEnv *env, jclass, jstring text) { LOGI("%s", detail::toUtf8(env, text).c_str()); }

    void logWhilePending(JNIEnv *env, jclass, jstring text) {
        jclass cls = env->FindClass("java/lang/UnsupportedOperationException");
        env->ThrowNew(cls, "pending");
        env->DeleteLocalRef(cls);
        LOGI("%s", detail::toUtf8(env, text).c_str());
    }

    std::u16string utf16(JNIEnv *env, jstring java) {
        const jchar *chars = env->GetStringChars(java, nullptr);
        std::u16string result(reinterpret_cast<const char16_t *>(chars), static_cast<std::size_t>(env->GetStringLength(java)));
        env->ReleaseStringChars(java, chars);
        return result;
    }

    class LogBridgeTest : public ::testing::Test {
    protected:
        void SetUp() override {
            env = jnirt::env();
            Registry registry;
            registry.add<LogSourceJ>().native<&logLine>("logLine").native<&logWhilePending>("logWhilePending");
            auto problems = detail::startup(env, registry, {&detail::classInfo<LogSourceJ>()});
            ASSERT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0]);
            LogSourceJ::reset(env);
            logging::setLevel(logging::Level::Verbose);
            logging::setForwarder(&detail::forwardLogToJava);
        }

        void TearDown() override {
            logging::setForwarder(nullptr);
            logging::setLevel(logging::Level::Debug);
            EXPECT_FALSE(env->ExceptionCheck());
        }

        std::string received() { return detail::toUtf8(env, LogSourceJ::received(env).get()); }

        JNIEnv *env = nullptr;
    };

    TEST_F(LogBridgeTest, ALineGoesToKotlinOnceAndNotToTheSink) {
        testing::internal::CaptureStderr();
        LOGI("hello %d", 5);
        std::string sink = testing::internal::GetCapturedStderr();
        EXPECT_EQ(received(), "4/logtest/hello 5");
        EXPECT_EQ(sink, "");
    }

    TEST_F(LogBridgeTest, LevelsArriveAsAndroidPriorities) {
        LOGV("v");
        LOGD("d");
        LOGI("i");
        LOGW("w");
        LOGE("e");
        EXPECT_EQ(received(), "2/logtest/v\n3/logtest/d\n4/logtest/i\n5/logtest/w\n6/logtest/e");
    }

    TEST_F(LogBridgeTest, TextIsUtf16OnTheJavaSide) {
        LOGI("%s", "привет \xF0\x9F\x98\x80");
        auto text = LogSourceJ::received(env);
        EXPECT_EQ(utf16(env, text.get()), u"4/logtest/привет \U0001F600");
    }

    TEST_F(LogBridgeTest, AThreadTheJvmDoesNotKnowIsNotAttachedForALogLine) {
        testing::internal::CaptureStderr();
        std::thread([] { LOGI("from a plain thread"); }).join();
        std::string sink = testing::internal::GetCapturedStderr();
        EXPECT_EQ(sink, "I/logtest: from a plain thread\n");
        EXPECT_EQ(received(), "");
    }

    TEST_F(LogBridgeTest, WhileAJavaExceptionIsPendingTheLineGoesToTheSink) {
        testing::internal::CaptureStderr();
        auto line = detail::toUtf8(env, LogSourceJ::probePending(env, env->NewStringUTF("while pending")).get());
        std::string sink = testing::internal::GetCapturedStderr();
        EXPECT_EQ(line, "java.lang.UnsupportedOperationException: pending");
        EXPECT_EQ(sink, "I/logtest: while pending\n");
        EXPECT_EQ(received(), "");
    }

    TEST_F(LogBridgeTest, ALineWrittenWhileOneIsForwardedGoesToTheSink) {
        LogSourceJ::logInsideReceiver(env, env->NewStringUTF("inner"));
        testing::internal::CaptureStderr();
        LOGI("outer");
        std::string sink = testing::internal::GetCapturedStderr();
        EXPECT_EQ(received(), "4/logtest/outer");
        EXPECT_EQ(sink, "I/logtest: inner\n");
    }

    TEST_F(LogBridgeTest, AReceiverThatThrowsLosesNothing) {
        LogSourceJ::receiverThrows(env, JNI_TRUE);
        testing::internal::CaptureStderr();
        LOGI("important");
        std::string sink = testing::internal::GetCapturedStderr();
        LogSourceJ::receiverThrows(env, JNI_FALSE);
        EXPECT_EQ(sink, "W/jnirt: the log receiver threw java.lang.IllegalStateException: receiver broke\n"
                        "I/logtest: important\n");
        EXPECT_FALSE(env->ExceptionCheck());
        LOGI("after");
        EXPECT_EQ(received(), "4/logtest/after");
    }

    TEST_F(LogBridgeTest, KotlinSetsTheNativeLevel) {
        LogSourceJ::setLevel(env, 5); // android.util.Log.WARN
        testing::internal::CaptureStderr();
        LOGI("filtered");
        LOGW("kept");
        std::string sink = testing::internal::GetCapturedStderr();
        EXPECT_EQ(received(), "5/logtest/kept");
        EXPECT_EQ(sink, "");
    }

    TEST_F(LogBridgeTest, ABadPriorityIsAnIllegalArgumentException) {
        try {
            LogSourceJ::setLevel(env, 99);
            FAIL() << "not thrown";
        } catch (const JavaException &e) {
            std::string what = e.what();
            EXPECT_EQ(what.rfind("java.lang.IllegalArgumentException: priority must be an android.util.Log constant "
                                 "(VERBOSE 2 ... ASSERT 7), got 99 (log_forward.cpp:", 0), 0u) << what;
        }
    }
} // namespace

#include <jnirt/support/logging/macros.h>

#include <jnirt/support/assert.h>

#include <gtest/gtest.h>

#include <csignal>
#include <string>
#include <unistd.h>

LOG_DEFINE_TAG("LoggingTest")

TEST(Logging, LevelFilterSkipsArgumentEvaluation) {
    jnirt::logging::setLevel(jnirt::logging::Level::Warn);
    int evaluated = 0;
    LOGD("%d", ++evaluated);
    EXPECT_EQ(evaluated, 0);
    LOGE("%d", ++evaluated);
    EXPECT_EQ(evaluated, 1);
    jnirt::logging::setLevel(jnirt::logging::Level::Debug);
}

namespace {
    using jnirt::logging::Level;

    int gSeen = 0;
    std::string gLast;
    bool gTake = false;

    bool recording(Level, const char *tag, const char *message) noexcept {
        ++gSeen;
        gLast = std::string(tag) + ": " + message;
        return gTake;
    }

    // If the failure path ever goes through the forwarder, the process leaves with code 0 instead of abort().
    bool leaving(Level, const char *, const char *) noexcept { _exit(0); }

    class Forwarding : public ::testing::Test {
    protected:
        void SetUp() override {
            gSeen = 0;
            gLast.clear();
            gTake = false;
            jnirt::logging::setForwarder(&recording);
        }

        void TearDown() override {
            jnirt::logging::setForwarder(nullptr);
            jnirt::logging::setLevel(Level::Debug);
        }
    };
}

TEST_F(Forwarding, SeesEveryLineThatPassedTheLevelFilter) {
    jnirt::logging::setLevel(Level::Info);
    LOGD("filtered out");
    EXPECT_EQ(gSeen, 0);
    LOGI("value %d", 5);
    EXPECT_EQ(gSeen, 1);
    EXPECT_EQ(gLast, "LoggingTest: value 5");
}

TEST_F(Forwarding, ATakenLineDoesNotReachTheSink) {
    gTake = true;
    testing::internal::CaptureStderr();
    LOGI("taken");
    std::string sink = testing::internal::GetCapturedStderr();
    EXPECT_EQ(gSeen, 1);
    EXPECT_EQ(sink, "");
}

TEST_F(Forwarding, ADeclinedLineGoesToTheSink) {
    gTake = false;
    testing::internal::CaptureStderr();
    LOGI("declined");
    std::string sink = testing::internal::GetCapturedStderr();
    EXPECT_EQ(gSeen, 1);
    EXPECT_EQ(sink, "I/LoggingTest: declined\n");
}

TEST_F(Forwarding, NoForwarderMeansTheSinkOnly) {
    jnirt::logging::setForwarder(nullptr);
    testing::internal::CaptureStderr();
    LOGI("plain");
    std::string sink = testing::internal::GetCapturedStderr();
    EXPECT_EQ(gSeen, 0);
    EXPECT_EQ(sink, "I/LoggingTest: plain\n");
}

TEST_F(Forwarding, TheFailurePathNeverGoesThroughTheForwarder) {
    jnirt::logging::setForwarder(&leaving);
    EXPECT_EXIT(JNIRT_FATAL("boom %d", 1), ::testing::KilledBySignal(SIGABRT), "boom 1");
}

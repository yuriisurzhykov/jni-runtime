#include <jnirt/support/logging/macros.h>

#include <gtest/gtest.h>

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

// Built twice from this file: with NDEBUG (release rules) and without (debug rules).

#include <jnirt/support/assert.h>

#include <gtest/gtest.h>

#include <string>

namespace {
    int gEvaluated = 0;

    int count() { return ++gEvaluated; }
}

class Assert : public ::testing::Test {
protected:
    void SetUp() override { gEvaluated = 0; }
};

// ---- REQUIRE / CHECK: the same in debug and release --------------------------------------------------------

TEST_F(Assert, RequirePassesWhenConditionHolds) {
    EXPECT_NO_THROW(JNIRT_REQUIRE(1 + 1 == 2, "never shown"));
}

TEST_F(Assert, RequireThrowsIllegalArgumentWithMessageAndLocation) {
    const int expectedLine = __LINE__ + 2;
    try {
        JNIRT_REQUIRE(false, "port must be positive, got %d", -1);
        FAIL() << "did not throw";
    } catch (const jnirt::IllegalArgument &e) {
        EXPECT_STREQ(e.what(), "port must be positive, got -1");
        EXPECT_EQ(e.line(), expectedLine);
        EXPECT_NE(std::string(e.file()).find("assert_test.cpp"), std::string::npos);
    }
}

TEST_F(Assert, CheckThrowsIllegalState) {
    try {
        JNIRT_CHECK(false, "camera is closed");
        FAIL() << "did not throw";
    } catch (const jnirt::IllegalState &e) {
        EXPECT_STREQ(e.what(), "camera is closed");
    }
}

TEST_F(Assert, RequireAndCheckThrowDifferentTypes) {
    EXPECT_THROW(JNIRT_REQUIRE(false, "x"), jnirt::IllegalArgument);
    EXPECT_THROW(JNIRT_CHECK(false, "x"), jnirt::IllegalState);
    EXPECT_THROW(JNIRT_REQUIRE(false, "x"), jnirt::ContractViolation);
    EXPECT_THROW(JNIRT_REQUIRE(false, "x"), std::logic_error);
}

TEST_F(Assert, MessageArgumentsAreEvaluatedOnlyOnFailure) {
    JNIRT_REQUIRE(true, "%d", count());
    JNIRT_CHECK(true, "%d", count());
    EXPECT_EQ(gEvaluated, 0);
    EXPECT_THROW(JNIRT_REQUIRE(false, "%d", count()), jnirt::IllegalArgument);
    EXPECT_EQ(gEvaluated, 1);
}

TEST_F(Assert, ConditionIsEvaluatedExactlyOnce) {
    JNIRT_REQUIRE(count() == 1, "x");
    EXPECT_EQ(gEvaluated, 1);
}

TEST_F(Assert, MacrosAreSingleStatements) {
    // Compiles and picks the right branch only if each macro expands to exactly one statement.
    bool other = false;
    if (gEvaluated == 0)
        JNIRT_REQUIRE(true, "x");
    else
        other = true;
    if (gEvaluated != 0)
        JNIRT_CHECK(true, "x");
    else
        other = true;
    EXPECT_TRUE(other);
}

TEST_F(Assert, RequireNobodyCanCatchTerminatesTheProcess) {
    // No entry() above it (pure C++ caller, own thread): nobody catches, the process dies.
    // A noexcept function stands in for "nobody catches": gtest's own death-test wrapper would catch a plain throw.
    auto noCatcher = []() noexcept { JNIRT_REQUIRE(false, "bad argument %d", 7); };
    EXPECT_DEATH(noCatcher(), "");
}

// ---- FATAL: always crashes --------------------------------------------------------------------------------

TEST_F(Assert, FatalAbortsWithLocationAndMessage) {
    EXPECT_DEATH(JNIRT_FATAL("unknown state %d", 7), "assert_test.cpp:[0-9]+: unknown state 7");
}

TEST_F(Assert, FatalSurvivesAVeryLongMessage) {
    std::string big(5000, 'x');
    EXPECT_DEATH(JNIRT_FATAL("%s", big.c_str()), "xxxxxxxx");   // truncated, but still a clean abort
}

// ---- ASSERT: debug crashes, release does nothing ----------------------------------------------------------

#ifndef NDEBUG
TEST_F(Assert, AssertPassesWhenConditionHolds) {
    JNIRT_ASSERT(count() == 1, "x");
    EXPECT_EQ(gEvaluated, 1);
}

TEST_F(Assert, DebugAssertAbortsWithConditionAndMessage) {
    int refCount = 0;
    EXPECT_DEATH(JNIRT_ASSERT(refCount > 0, "refcount underflow: %d", refCount),
                 "assertion `refCount > 0` failed: refcount underflow: 0");
}

TEST_F(Assert, ConditionTextMayContainPercentSigns) {
    int n = 3;
    EXPECT_DEATH(JNIRT_ASSERT(n % 2 == 0, "n is odd"), "n % 2 == 0");   // must not be read as a format
}
#else
TEST_F(Assert, ReleaseAssertDoesNotEvaluateAnything) {
    JNIRT_ASSERT(count() > 100, "%d", count());
    EXPECT_EQ(gEvaluated, 0);
}

TEST_F(Assert, ReleaseAssertDoesNotAbort) {
    JNIRT_ASSERT(false, "would crash in debug");
    SUCCEED();
}
#endif

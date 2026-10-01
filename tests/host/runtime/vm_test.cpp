// jnirt::setVm / jnirt::vm without a real JVM: the pointer is only stored, never used.
// Death tests run in the "threadsafe" style, so each starts from a fresh process with no VM set.

#include <jnirt/runtime/vm.h>

#include <gtest/gtest.h>

namespace {
    JavaVM *fakeVm(uintptr_t value) { return reinterpret_cast<JavaVM *>(value); }
}

class Vm : public ::testing::Test {
protected:
    static void SetUpTestSuite() { GTEST_FLAG_SET(death_test_style, "threadsafe"); }
};

TEST_F(Vm, VmBeforeSetVmCrashesWithHint) {
    EXPECT_DEATH(jnirt::vm(), "jnirt is not initialized.*call jnirt::init\\(vm, \\.\\.\\.\\) from JNI_OnLoad");
}

TEST_F(Vm, SetVmNullCrashes) {
    EXPECT_DEATH(jnirt::setVm(nullptr), "jnirt::setVm\\(nullptr\\)");
}

TEST_F(Vm, DifferentVmCrashes) {
    EXPECT_DEATH({
                 jnirt::setVm(fakeVm(0x1000));
                 jnirt::setVm(fakeVm(0x2000));
                 }, "two different JavaVM in one process");
}

TEST_F(Vm, SameVmTwiceIsFineAndVmReturnsIt) {
    jnirt::setVm(fakeVm(0x1000));
    jnirt::setVm(fakeVm(0x1000));
    EXPECT_EQ(jnirt::vm(), fakeVm(0x1000));
}

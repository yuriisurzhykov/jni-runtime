// One JVM per test binary, for the tests that need a real one: created before the first test and never destroyed
// (HotSpot can not create a second VM in one process). -Xcheck:jni is on, and the Java classes of tests/host/java
// are on the class path. Include it in exactly one .cpp per test binary.

#ifndef JNIRT_TESTS_TEST_JVM_H
#define JNIRT_TESTS_TEST_JVM_H

#pragma once
#include <jnirt/runtime/vm.h>

#include <gtest/gtest.h>

#include <string>

namespace jnirt::test {
    class JvmEnvironment : public ::testing::Environment {
    public:
        void SetUp() override {
            std::string classPath = std::string("-Djava.class.path=") + JNIRT_TEST_CLASSPATH;
            JavaVMOption options[] = {
                {const_cast<char *>("-Xcheck:jni"), nullptr},
                {const_cast<char *>(classPath.c_str()), nullptr},
            };
            JavaVMInitArgs args{JNI_VERSION_1_6, 2, options, JNI_FALSE};
            JavaVM *jvm = nullptr;
            JNIEnv *env = nullptr;
            ASSERT_EQ(JNI_CreateJavaVM(&jvm, reinterpret_cast<void **>(&env), &args), JNI_OK);
            jnirt::setVm(jvm);
        }
    };

    inline const auto *gJvmEnvironment = ::testing::AddGlobalTestEnvironment(new JvmEnvironment);
} // namespace jnirt::test

#endif //JNIRT_TESTS_TEST_JVM_H

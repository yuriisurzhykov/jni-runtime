#include <jnirt/support/error.h>

#include <gtest/gtest.h>

#include <exception>
#include <type_traits>

namespace testlib {
    enum class Errc { AuthFailed = 1, StreamLost = 2 };
}
namespace otherlib {
    enum class Errc { Other = 1 };  // same numbers as testlib on purpose
}
JNIRT_ERROR_DOMAIN(testlib::Errc, "testlib")
JNIRT_ERROR_DOMAIN(otherlib::Errc, "otherlib")

TEST(Error, CarriesDomainCodeAndFormattedMessage) {
    jnirt::Error e(testlib::Errc::AuthFailed, "camera %s rejected login (%d)", "10.0.0.5", 3);
    EXPECT_STREQ(e.domain(), "testlib");
    EXPECT_EQ(e.code(), 1);
    EXPECT_STREQ(e.what(), "camera 10.0.0.5 rejected login (3)");
}

TEST(Error, DomainIsBoundToTheEnum) {
    EXPECT_STREQ(jnirt::Error(otherlib::Errc::Other, "x").domain(), "otherlib");
    EXPECT_STREQ(jnirt::Error(jnirt::Errc::Timeout, "x").domain(), "jnirt");
    EXPECT_EQ(jnirt::Error(jnirt::Errc::Io, "x").code(), 3);
}

TEST(Error, IsComparesDomainAndCode) {
    jnirt::Error e(testlib::Errc::AuthFailed, "x");
    EXPECT_TRUE(e.is(testlib::Errc::AuthFailed));
    EXPECT_FALSE(e.is(testlib::Errc::StreamLost));
    EXPECT_FALSE(e.is(otherlib::Errc::Other));   // code 1 in another domain is a different error
}

TEST(Error, LongMessageIsNotTruncated) {
    std::string big(5000, 'x');
    jnirt::Error e(jnirt::Errc::Internal, "%s", big.c_str());
    EXPECT_EQ(std::string(e.what()), big);
}

TEST(Error, CopyKeepsEverythingAndCannotThrow) {
    static_assert(std::is_nothrow_copy_constructible<jnirt::Error>::value, "");
    jnirt::Error original(testlib::Errc::StreamLost, "lost after %d s", 5);
    jnirt::Error copy = original;
    EXPECT_STREQ(copy.what(), "lost after 5 s");
    EXPECT_EQ(copy.code(), 2);
    EXPECT_STREQ(copy.domain(), "testlib");
}

TEST(Error, IsCatchableAsStdException) {
    try {
        throw jnirt::Error(jnirt::Errc::NotFound, "missing");
    } catch (const std::exception &e) {
        EXPECT_STREQ(e.what(), "missing");
        return;
    }
    FAIL() << "not thrown";
}

TEST(Error, CauseSurvivesThroughNestedException) {
    try {
        try {
            throw jnirt::Error(jnirt::Errc::Io, "socket closed");
        } catch (...) {
            std::throw_with_nested(jnirt::Error(testlib::Errc::StreamLost, "stream lost"));
        }
    } catch (const jnirt::Error &outer) {
        EXPECT_TRUE(outer.is(testlib::Errc::StreamLost));
        try {
            std::rethrow_if_nested(outer);
            FAIL() << "cause is lost";
        } catch (const jnirt::Error &cause) {
            EXPECT_TRUE(cause.is(jnirt::Errc::Io));
            EXPECT_STREQ(cause.what(), "socket closed");
        }
    }
}

TEST(ContractViolation, KeepsMessageAndLocationApart) {
    jnirt::IllegalArgument e("bad port", "camera.cpp", 42);
    EXPECT_STREQ(e.what(), "bad port");
    EXPECT_STREQ(e.file(), "camera.cpp");
    EXPECT_EQ(e.line(), 42);

    const jnirt::ContractViolation &base = e;   // entry() catches the base for logging
    EXPECT_EQ(base.line(), 42);
}

TEST(ContractViolation, ArgumentAndStateAreDifferentTypes) {
    static_assert(!std::is_base_of<jnirt::IllegalArgument, jnirt::IllegalState>::value, "");
    static_assert(!std::is_base_of<jnirt::IllegalState, jnirt::IllegalArgument>::value, "");
    static_assert(std::is_nothrow_copy_constructible<jnirt::IllegalArgument>::value, "");
}

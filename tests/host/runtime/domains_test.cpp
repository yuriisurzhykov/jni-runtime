// The startup check of error domains (detail::checkDomains) with lists of our own: the real list of a test binary
// can not be changed. No JVM needed.

#include "class_cache_internal.h"

#include <gtest/gtest.h>

using namespace jnirt;
using detail::DomainDeclaration;
using detail::Problems;

namespace {
    Problems check(const std::vector<DomainDeclaration> &declared) {
        Problems problems;
        detail::checkDomains(declared, problems);
        return problems;
    }

    TEST(Domains, DifferentNamesAreFine) {
        EXPECT_TRUE(check({{"jnirt", "jnirt::Errc"}, {"wificameras", "wificameras::Errc"}, {"sync", "sync::Errc"}}).empty());
    }

    TEST(Domains, TheSameEnumSeenTwiceIsFine) {
        EXPECT_TRUE(check({{"sync", "sync::Errc"}, {"sync", "sync::Errc"}}).empty());
    }

    TEST(Domains, TwoEnumsWithOneNameAreOneProblemThatNamesBoth) {
        auto problems = check({{"sync", "sync::Errc"}, {"wificameras", "wificameras::Errc"}, {"sync", "other::Errc"}});
        ASSERT_EQ(problems.size(), 1u);
        EXPECT_EQ(problems[0],
                  "error domain \"sync\" is declared for 2 different enums: sync::Errc, other::Errc\n"
                  "  why: Kotlin receives only the domain and the number, so two enums with one domain can not be told "
                  "apart.\n"
                  "  hint: every library needs its own domain name in JNIRT_ERROR_DOMAIN.");
    }

    TEST(Domains, EachClashingNameIsReportedOnce) {
        auto problems = check({{"a", "x::E"}, {"a", "y::E"}, {"a", "z::E"}, {"b", "x::F"}, {"b", "y::F"}});
        EXPECT_EQ(problems.size(), 2u);
    }

    TEST(Domains, TheJnirtNameBelongsToJnirtErrc) {
        auto problems = check({{"jnirt", "jnirt::Errc"}, {"jnirt", "mylib::Errc"}});
        ASSERT_EQ(problems.size(), 1u);
        EXPECT_EQ(problems[0],
                  "error domain \"jnirt\" is reserved for jnirt::Errc, but mylib::Errc declares it\n"
                  "  hint: use the name of your library as the domain.");
    }

    TEST(Domains, AnEmptyNameIsAProblem) {
        auto problems = check({{"", "mylib::Errc"}});
        ASSERT_EQ(problems.size(), 1u);
        EXPECT_EQ(problems[0],
                  "error domain of mylib::Errc is empty\n"
                  "  hint: JNIRT_ERROR_DOMAIN(Enum, \"name\") needs a name; use the name of your library.");
    }

    TEST(Domains, TheListOfThisProgramHasNoProblem) {
        EXPECT_TRUE(check(detail::declaredDomains()).empty());
    }
} // namespace

// What the startup says when a method is not found for one of the usual Kotlin reasons (ADR-0008 §9): a mangled name,
// a companion object, default parameter values. The matcher is pure; the rest runs against a real JVM with a class
// that has the shapes Kotlin generates (tests/host/java/jnirt/test/KotlinShapes.java).

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>

#include "class_cache_internal.h"
#include "kotlin_hints.h"
#include "test_jvm.h"

#include <string>
#include <vector>

using namespace jnirt;

namespace {
    using detail::mangledNameHint;
    using Names = std::vector<std::string>;

    bool contains(const std::string &whole, const std::string &part) { return whole.find(part) != std::string::npos; }

    // ---- the matcher ----

    TEST(MangledNameHint, AValueClassParameterAddsAHash) {
        std::string hint = mangledNameHint("open", Names{"open-Ab3xQ9z", "close"});
        EXPECT_TRUE(contains(hint, "`open-Ab3xQ9z`")) << hint;
        EXPECT_TRUE(contains(hint, "value class")) << hint;
        EXPECT_TRUE(contains(hint, "underlying type")) << hint;
        EXPECT_EQ(hint.rfind("\n  hint: ", 0), 0u) << "starts with a hint line";
    }

    TEST(MangledNameHint, AnInternalFunctionAddsTheModule) {
        std::string hint = mangledNameHint("open", Names{"open$camera_release"});
        EXPECT_TRUE(contains(hint, "`open$camera_release`")) << hint;
        EXPECT_TRUE(contains(hint, "`internal`")) << hint;
        EXPECT_TRUE(contains(hint, "`public` or `private`")) << hint;
    }

    TEST(MangledNameHint, DefaultParametersHaveAMethodOfTheirOwn) {
        std::string hint = mangledNameHint("close", Names{"close", "close$default"});
        EXPECT_TRUE(contains(hint, "`close$default`")) << hint;
        EXPECT_TRUE(contains(hint, "describe the full signature")) << hint;
        EXPECT_FALSE(contains(hint, "`internal`")) << "$default is not a module name: " << hint;
    }

    TEST(MangledNameHint, NamesThatOnlyLookSimilarAreLeftAlone) {
        EXPECT_EQ(mangledNameHint("open", Names{"opened", "openFile", "reopen", "access$open", "open"}), "");
        EXPECT_EQ(mangledNameHint("open", Names{}), "");
    }

    TEST(MangledNameHint, SeveralKindsAtOnceAreSeveralLines) {
        std::string hint = mangledNameHint("x", Names{"x-1a", "x$mod", "x$default"});
        EXPECT_TRUE(contains(hint, "`x-1a`"));
        EXPECT_TRUE(contains(hint, "`x$mod`"));
        EXPECT_TRUE(contains(hint, "`x$default`"));
    }

    // ---- against a real JVM ----

    struct KotlinShapesJ : JavaClass<KotlinShapesJ> {
        static constexpr const char *name = "jnirt/test/KotlinShapes";

        static inline Constructor<KotlinShapesJ, void()> create;
        static inline Method<KotlinShapesJ, jint(jint)> open{"open"}; // Java has open$module
        static inline Method<KotlinShapesJ, jint(jint, jint)> close{"close"}; // found
        static inline StaticMethod<KotlinShapesJ, jint(jint)> count{"count"}; // Java has Companion.count
        static inline Method<KotlinShapesJ, jint(jint)> missing{"missing"}; // nothing like it
    };

    jint ping(JNIEnv *, jobject, jint x) { return x; }

    TEST(KotlinHints, TheStartupNamesTheKotlinReason) {
        JNIEnv *env = jnirt::env();
        Registry registry;
        registry.add<KotlinShapesJ>().native<&ping>("ping");
        auto problems = detail::startup(env, registry, {&detail::classInfo<KotlinShapesJ>()});

        std::string all;
        for (const std::string &problem: problems) all += problem + "\n";
        ASSERT_EQ(problems.size(), 5u) << all; // the 5th: `ping$module` is native in Java and nothing is registered for it

        const std::string *open = nullptr, *count = nullptr, *missing = nullptr, *ping = nullptr;
        for (const std::string &problem: problems) {
            if (contains(problem, "jnirt.test.KotlinShapes.open ")) open = &problem;
            if (contains(problem, "jnirt.test.KotlinShapes.count ")) count = &problem;
            if (contains(problem, "jnirt.test.KotlinShapes.missing ")) missing = &problem;
            if (contains(problem, "jnirt.test.KotlinShapes.ping ")) ping = &problem;
        }
        ASSERT_TRUE(open && count && missing && ping) << all;

        EXPECT_TRUE(contains(*open, ": not found")) << *open;
        EXPECT_TRUE(contains(*open, "`open$module`")) << *open;
        EXPECT_TRUE(contains(*open, "`internal`")) << *open;

        EXPECT_TRUE(contains(*count, "jnirt.test.KotlinShapes$Companion has count (I)I")) << *count;
        EXPECT_TRUE(contains(*count, "@JvmStatic")) << *count;

        EXPECT_EQ(*missing, "method jnirt.test.KotlinShapes.missing (I)I: not found") << "no hint without a reason";

        EXPECT_TRUE(contains(*ping, "native method jnirt.test.KotlinShapes.ping (I)I: not found")) << *ping;
        EXPECT_TRUE(contains(*ping, "`ping$module`")) << *ping;
        EXPECT_FALSE(env->ExceptionCheck()) << "a lookup that failed must not leave an exception";
    }

    TEST(KotlinHints, AMethodThatIsThereGetsNoHint) {
        JNIEnv *env = jnirt::env();
        Registry registry;
        registry.add<KotlinShapesJ>().native<&ping>("ping");
        auto problems = detail::startup(env, registry, {&detail::classInfo<KotlinShapesJ>()});
        for (const std::string &problem: problems) EXPECT_FALSE(contains(problem, "KotlinShapes.close ")) << problem;
    }
} // namespace

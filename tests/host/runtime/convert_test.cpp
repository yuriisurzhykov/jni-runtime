// The conversion table (ADR-0008) at work: natives written with C++ types, and Java called with C++ types, against a
// real JVM (-Xcheck:jni). Each test goes through both layers: class_cache converts the arguments, the registered native
// converts them back, and the result takes the same way home.
//
// The first part is static_assert: the descriptors and signatures of the new types, no JVM needed.

#include <jnirt/runtime/convert.h>
#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/runtime/signature.h>
#include <jnirt/support/error.h>

#include "class_cache_internal.h"
#include "ref_internal.h"
#include "registry_internal.h"
#include "test_jvm.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

using namespace jnirt;

namespace {
    using std::string_view;

    // ---- compile time ----

    static_assert(string_view(descriptor<bool>) == "Z");
    static_assert(string_view(descriptor<std::string>) == "Ljava/lang/String;");
    static_assert(string_view(descriptor<const std::string &>) == "Ljava/lang/String;");
    static_assert(string_view(descriptor<std::u16string>) == "Ljava/lang/String;");
    static_assert(string_view(descriptor<std::string_view>) == "Ljava/lang/String;");
    static_assert(string_view(descriptor<std::optional<std::string> >) == "Ljava/lang/String;");
    static_assert(string_view(descriptor<std::optional<jboolean> >) == "Ljava/lang/Boolean;");
    static_assert(string_view(descriptor<std::optional<bool> >) == "Ljava/lang/Boolean;");
    static_assert(string_view(descriptor<std::optional<jbyte> >) == "Ljava/lang/Byte;");
    static_assert(string_view(descriptor<std::optional<jchar> >) == "Ljava/lang/Character;");
    static_assert(string_view(descriptor<std::optional<jshort> >) == "Ljava/lang/Short;");
    static_assert(string_view(descriptor<std::optional<jint> >) == "Ljava/lang/Integer;");
    static_assert(string_view(descriptor<std::optional<jlong> >) == "Ljava/lang/Long;");
    static_assert(string_view(descriptor<std::optional<jfloat> >) == "Ljava/lang/Float;");
    static_assert(string_view(descriptor<std::optional<jdouble> >) == "Ljava/lang/Double;");
    static_assert(string_view(signature<bool(const std::string &, std::optional<jint>)>) ==
                  "(Ljava/lang/String;Ljava/lang/Integer;)Z");
    static_assert(string_view(signature<std::optional<std::string>()>) == "()Ljava/lang/String;");
    static_assert(string_view(signature<void(std::string &&, jint)>) == "(Ljava/lang/String;I)V");

    static_assert(std::is_same_v<detail::JniOf<std::string>, jstring>);
    static_assert(std::is_same_v<detail::JniOf<std::optional<jint> >, jobject>);
    static_assert(std::is_same_v<detail::JniOf<bool>, jboolean>);
    static_assert(std::is_same_v<detail::ValueOf<const std::string &>, std::string>);
    static_assert(detail::rejectsNull<std::string>);
    static_assert(!detail::rejectsNull<std::optional<std::string> >);
    static_assert(!detail::rejectsNull<jstring>);
    static_assert(!detail::rejectsNull<jint>);

    // ---- the class under test ----

    int gCalls = 0;

    std::string echoString(JNIEnv *, jobject, const std::string &text) { return text; }

    std::optional<std::string> echoOptString(JNIEnv *, jobject, const std::optional<std::string> &text) { return text; }

    std::u16string echoUtf16(JNIEnv *, jobject, const std::u16string &text) { return text; }

    bool echoBool(JNIEnv *, jobject, bool value) { return value; }

    std::string joinThree(JNIEnv *, jobject, const std::string &a, const std::string &b, jint count) {
        return a + b + std::to_string(count);
    }

    std::string joinOpt(JNIEnv *, jobject, const std::string &a, const std::optional<std::string> &b) {
        return a + b.value_or("-");
    }

    std::string byValue(JNIEnv *, jobject, std::string text) { return text + "!"; }

    std::string byRvalue(JNIEnv *, jobject, std::string &&text) { return std::move(text) + "?"; }

    std::string staticEcho(JNIEnv *, jclass, const std::string &text) { return text; }

    std::optional<bool> echoOptBool(JNIEnv *, jobject, std::optional<bool> value) { return value; }

    std::optional<jbyte> echoOptByte(JNIEnv *, jobject, std::optional<jbyte> value) { return value; }

    std::optional<jchar> echoOptChar(JNIEnv *, jobject, std::optional<jchar> value) { return value; }

    std::optional<jshort> echoOptShort(JNIEnv *, jobject, std::optional<jshort> value) { return value; }

    std::optional<jint> echoOptInt(JNIEnv *, jobject, std::optional<jint> value) { return value; }

    std::optional<jlong> echoOptLong(JNIEnv *, jobject, std::optional<jlong> value) { return value; }

    std::optional<jfloat> echoOptFloat(JNIEnv *, jobject, std::optional<jfloat> value) { return value; }

    std::optional<jdouble> echoOptDouble(JNIEnv *, jobject, std::optional<jdouble> value) { return value; }

    jint lengthOf(JNIEnv *, jobject, const std::string &text) {
        ++gCalls;
        return static_cast<jint>(text.size());
    }

    struct ConverterJ : JavaClass<ConverterJ> {
        static constexpr const char *name = "jnirt/test/Converter";

        static inline Constructor<ConverterJ, void()> create;

        // The natives, described with C++ types (both layers meet here) ...
        static inline Method<ConverterJ, std::string(std::string)> echoString{"echoString"};
        static inline Method<ConverterJ, std::optional<std::string>(std::optional<std::string>)> echoOptString{
            "echoOptString"
        };
        static inline Method<ConverterJ, std::u16string(std::u16string)> echoUtf16{"echoUtf16"};
        static inline Method<ConverterJ, bool(bool)> echoBool{"echoBool"};
        static inline Method<ConverterJ, std::string(std::string, std::string, jint)> joinThree{"joinThree"};
        static inline Method<ConverterJ, std::string(std::string, std::optional<std::string>)> joinOpt{"joinOpt"};
        static inline Method<ConverterJ, std::string(std::string)> byValue{"byValue"};
        static inline Method<ConverterJ, std::string(std::string)> byRvalue{"byRvalue"};
        static inline StaticMethod<ConverterJ, std::string(std::string)> staticEcho{"staticEcho"};
        static inline Method<ConverterJ, std::optional<bool>(std::optional<bool>)> echoOptBool{"echoOptBool"};
        static inline Method<ConverterJ, std::optional<jbyte>(std::optional<jbyte>)> echoOptByte{"echoOptByte"};
        static inline Method<ConverterJ, std::optional<jchar>(std::optional<jchar>)> echoOptChar{"echoOptChar"};
        static inline Method<ConverterJ, std::optional<jshort>(std::optional<jshort>)> echoOptShort{"echoOptShort"};
        static inline Method<ConverterJ, std::optional<jint>(std::optional<jint>)> echoOptInt{"echoOptInt"};
        static inline Method<ConverterJ, std::optional<jlong>(std::optional<jlong>)> echoOptLong{"echoOptLong"};
        static inline Method<ConverterJ, std::optional<jfloat>(std::optional<jfloat>)> echoOptFloat{"echoOptFloat"};
        static inline Method<ConverterJ, std::optional<jdouble>(std::optional<jdouble>)> echoOptDouble{
            "echoOptDouble"
        };
        static inline Method<ConverterJ, jint(std::string)> lengthOf{"lengthOf"};

        // ... and the same natives with raw JNI types, to send a null that C++ types can not hold.
        static inline Method<ConverterJ, jstring(jstring)> echoStringRaw{"echoString"};
        static inline Method<ConverterJ, jstring(jstring, jstring, jint)> joinThreeRaw{"joinThree"};
        static inline Method<ConverterJ, jint(jstring)> lengthOfRaw{"lengthOf"};

        // Java methods that native code calls.
        static inline Field<ConverterJ, std::string> name_{"name"};
        static inline Field<ConverterJ, std::optional<std::string> > nick{"nick"};
        static inline StaticField<ConverterJ, std::string> title{"title"};
        static inline Method<ConverterJ, std::string(std::string)> greet{"greet"};
        static inline Method<ConverterJ, std::string(std::string_view)> greetView{"greet"};
        static inline Method<ConverterJ, std::optional<std::string>(std::optional<std::string>)> greetOpt{"greetOpt"};
        static inline Method<ConverterJ, std::string()> nothing{"nothing"};
        static inline Method<ConverterJ, std::optional<std::string>()> nothingOpt{"nothing"};
        static inline Method<ConverterJ, jstring()> nothingRaw{"nothing"};
        static inline StaticMethod<ConverterJ, std::string(std::string)> shout{"shout"};
        static inline Method<ConverterJ, std::optional<jint>(std::optional<jint>)> boxedInt{"boxedInt"};
        static inline Method<ConverterJ, void(std::string, jint, bool)> takesTwo{"takesTwo"};
    };

    void registerConverter(Registry &r) {
        r.add<ConverterJ>()
                .native<&echoString>("echoString")
                .native<&echoOptString>("echoOptString")
                .native<&echoUtf16>("echoUtf16")
                .native<&echoBool>("echoBool")
                .native<&joinThree>("joinThree")
                .native<&joinOpt>("joinOpt")
                .native<&byValue>("byValue")
                .native<&byRvalue>("byRvalue")
                .native<&staticEcho>("staticEcho")
                .native<&echoOptBool>("echoOptBool")
                .native<&echoOptByte>("echoOptByte")
                .native<&echoOptChar>("echoOptChar")
                .native<&echoOptShort>("echoOptShort")
                .native<&echoOptInt>("echoOptInt")
                .native<&echoOptLong>("echoOptLong")
                .native<&echoOptFloat>("echoOptFloat")
                .native<&echoOptDouble>("echoOptDouble")
                .native<&lengthOf>("lengthOf");
    }

    class ConvertTest : public ::testing::Test {
    protected:
        void SetUp() override {
            env = jnirt::env();
            Registry registry;
            registerConverter(registry);
            auto problems = detail::startup(env, registry, {&detail::classInfo<ConverterJ>()});
            ASSERT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0]);
            self = ConverterJ::create(env);
            localRefsBefore = detail::liveLocalRefsOnThisThread();
        }

        // Nothing leaked, nothing pending.
        void TearDown() override {
            EXPECT_FALSE(env->ExceptionCheck());
            EXPECT_EQ(detail::liveLocalRefsOnThisThread(), localRefsBefore) << "local references leaked";
        }

        jobject obj() const { return self.get(); }

        // What Java says about the exception: "java.lang.IllegalArgumentException: argument 1 is null ...".
        template<class F>
        std::string javaFailure(F &&call) {
            try {
                call();
            } catch (const JavaException &e) {
                return e.what();
            }
            return "no exception";
        }

        static bool contains(const std::string &whole, const std::string &part) {
            return whole.find(part) != std::string::npos;
        }

        JNIEnv *env = nullptr;
        LocalRef<jobject> self;
        int localRefsBefore = 0;
    };

    // ---- strings ----

    TEST_F(ConvertTest, AStringGoesThereAndBackUnchanged) {
        for (const std::string &text: {
                 std::string(""), std::string("hello"), std::string("Привет, мир"), std::string("камера 📷 #1"),
                 std::string("a\0b", 3), std::string("\xF0\x9F\x98\x80"), std::string(300, 'x')
             }) {
            EXPECT_EQ(ConverterJ::echoString(env, obj(), text), text) << text;
        }
    }

    TEST_F(ConvertTest, ALongTextTakesTheHeapPath) {
        std::string ascii(1 << 20, 'a');
        EXPECT_EQ(ConverterJ::echoString(env, obj(), ascii), ascii);
        std::string cyrillic;
        for (int i = 0; i < 200000; ++i) cyrillic += "ж"; // 400 000 bytes, 200 000 UTF-16 units
        EXPECT_EQ(ConverterJ::echoString(env, obj(), cyrillic), cyrillic);
    }

    TEST_F(ConvertTest, AnUnpairedSurrogateFromJavaBecomesReplacement) {
        jchar lone[] = {u'a', 0xD800, u'b'}; // valid in Java, impossible in UTF-8
        LocalRef<jstring> java(env, env->NewString(lone, 3));
        EXPECT_EQ(fromJava<std::string>(env, java.get()), "a\xEF\xBF\xBD" "b");

        jchar low[] = {0xDC00};
        LocalRef<jstring> javaLow(env, env->NewString(low, 1));
        EXPECT_EQ(fromJava<std::string>(env, javaLow.get()), "\xEF\xBF\xBD");
    }

    TEST_F(ConvertTest, InvalidUtf8FromCppBecomesReplacementInJava) {
        EXPECT_EQ(ConverterJ::echoString(env, obj(), std::string("a\xFF" "b")), "a\xEF\xBF\xBD" "b");
        EXPECT_EQ(ConverterJ::echoString(env, obj(), std::string("\xE2\x82")), "\xEF\xBF\xBD"); // a cut-off character
    }

    TEST_F(ConvertTest, Utf16GoesThroughUnchangedEvenWhenItIsNotText) {
        std::u16string odd = u"a";
        odd += char16_t(0xD800); // an unpaired surrogate: UTF-16 does not care
        odd += u"bé€";
        odd += u"\U0001F600";
        EXPECT_EQ(ConverterJ::echoUtf16(env, obj(), odd), odd);
        EXPECT_EQ(ConverterJ::echoUtf16(env, obj(), std::u16string()), std::u16string());
    }

    TEST_F(ConvertTest, ANativeTakesAStringByValueByReferenceAndByRvalue) {
        EXPECT_EQ(ConverterJ::byValue(env, obj(), "x"), "x!");
        EXPECT_EQ(ConverterJ::byRvalue(env, obj(), "x"), "x?");
        EXPECT_EQ(ConverterJ::echoString(env, obj(), "x"), "x");
    }

    TEST_F(ConvertTest, AStaticNativeConverts) { EXPECT_EQ(ConverterJ::staticEcho(env, "статика"), "статика"); }

    TEST_F(ConvertTest, SeveralParametersAreConvertedInOrder) {
        EXPECT_EQ(ConverterJ::joinThree(env, obj(), "a", "б", 7), "aб7");
        EXPECT_EQ(ConverterJ::joinOpt(env, obj(), "a", "b"), "ab");
        EXPECT_EQ(ConverterJ::joinOpt(env, obj(), "a", std::nullopt), "a-");
    }

    TEST_F(ConvertTest, ANativeIsNotCalledWithANullThatItCanNotHold) {
        int before = gCalls;
        std::string failure = javaFailure([&] { ConverterJ::lengthOfRaw(env, obj(), nullptr); });
        EXPECT_TRUE(contains(failure, "java.lang.IllegalArgumentException")) << failure;
        EXPECT_TRUE(contains(failure, "argument 1 is null")) << failure;
        EXPECT_TRUE(contains(failure, "Ljava/lang/String;")) << failure;
        EXPECT_EQ(gCalls, before) << "the native ran";
    }

    TEST_F(ConvertTest, TheMessageNamesTheArgumentAndHasNoPlaceInJnirt) {
        LocalRef<jstring> fine = toJava(env, std::string("x"));
        std::string failure = javaFailure([&] { ConverterJ::joinThreeRaw(env, obj(), fine.get(), nullptr, 1); });
        EXPECT_TRUE(contains(failure, "argument 2 is null")) << failure;
        EXPECT_FALSE(contains(failure, ".h:")) << failure;
        EXPECT_FALSE(contains(failure, ".cpp:")) << failure;
    }

    TEST_F(ConvertTest, AnEchoOfARawNullReachesTheNativeAsAnError) {
        std::string failure = javaFailure([&] { ConverterJ::echoStringRaw(env, obj(), nullptr); });
        EXPECT_TRUE(contains(failure, "IllegalArgumentException")) << failure;
    }

    // ---- bool ----

    TEST_F(ConvertTest, BoolGoesBothWays) {
        EXPECT_TRUE(ConverterJ::echoBool(env, obj(), true));
        EXPECT_FALSE(ConverterJ::echoBool(env, obj(), false));
    }

    // ---- optional ----

    TEST_F(ConvertTest, AnOptionalStringIsAStringOrNull) {
        EXPECT_EQ(ConverterJ::echoOptString(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::echoOptString(env, obj(), std::optional<std::string>("x")), "x");
        EXPECT_EQ(ConverterJ::echoOptString(env, obj(), std::optional<std::string>("")), "");
    }

    TEST_F(ConvertTest, EveryNumberBoxesAndUnboxes) {
        EXPECT_EQ(ConverterJ::echoOptBool(env, obj(), true), std::optional<bool>(true));
        EXPECT_EQ(ConverterJ::echoOptBool(env, obj(), false), std::optional<bool>(false));
        EXPECT_EQ(ConverterJ::echoOptBool(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::echoOptByte(env, obj(), jbyte(-5)), std::optional<jbyte>(-5));
        EXPECT_EQ(ConverterJ::echoOptChar(env, obj(), jchar(0x20AC)), std::optional<jchar>(0x20AC));
        EXPECT_EQ(ConverterJ::echoOptShort(env, obj(), jshort(-300)), std::optional<jshort>(-300));
        EXPECT_EQ(ConverterJ::echoOptInt(env, obj(), 1 << 30), std::optional<jint>(1 << 30));
        EXPECT_EQ(ConverterJ::echoOptLong(env, obj(), jlong(1) << 40), std::optional<jlong>(jlong(1) << 40));
        EXPECT_EQ(ConverterJ::echoOptFloat(env, obj(), 1.5f), std::optional<jfloat>(1.5f));
        EXPECT_EQ(ConverterJ::echoOptDouble(env, obj(), 2.25), std::optional<jdouble>(2.25));
        EXPECT_EQ(ConverterJ::echoOptByte(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::echoOptChar(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::echoOptShort(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::echoOptInt(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::echoOptLong(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::echoOptFloat(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::echoOptDouble(env, obj(), std::nullopt), std::nullopt);
    }

    // ---- calling Java with C++ types ----

    TEST_F(ConvertTest, JavaIsCalledWithConvertedArgumentsAndGivesConvertedResults) {
        EXPECT_EQ(ConverterJ::greet(env, obj(), "мир"), "hello мир");
        EXPECT_EQ(ConverterJ::greetView(env, obj(), std::string_view("view")), "hello view");
        EXPECT_EQ(ConverterJ::greetOpt(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ConverterJ::greetOpt(env, obj(), std::optional<std::string>("x")), "hello x");
        EXPECT_EQ(ConverterJ::shout(env, "камера"), "КАМЕРА");
        EXPECT_EQ(ConverterJ::boxedInt(env, obj(), 41), std::optional<jint>(41));
    }

    TEST_F(ConvertTest, SeveralKindsOfArgumentsInOneCall) {
        ConverterJ::takesTwo(env, obj(), "a", 3, true);
        EXPECT_EQ(ConverterJ::name_.get(env, obj()), "a:3:true");
    }

    TEST_F(ConvertTest, FieldsHoldConvertedValues) {
        ConverterJ::name_.set(env, obj(), "Двор");
        EXPECT_EQ(ConverterJ::name_.get(env, obj()), "Двор");
        EXPECT_EQ(ConverterJ::nick.get(env, obj()), std::nullopt);
        ConverterJ::nick.set(env, obj(), std::optional<std::string>("ник"));
        EXPECT_EQ(ConverterJ::nick.get(env, obj()), "ник");
        ConverterJ::nick.set(env, obj(), std::nullopt);
        EXPECT_EQ(ConverterJ::nick.get(env, obj()), std::nullopt);
        EXPECT_EQ(ConverterJ::title.get(env), "camera");
        ConverterJ::title.set(env, "новый");
        EXPECT_EQ(ConverterJ::title.get(env), "новый");
        ConverterJ::title.set(env, "camera");
    }

    TEST_F(ConvertTest, ANullResultIsAnOptionalOrAnErrorOfTheDescription) {
        EXPECT_EQ(ConverterJ::nothingOpt(env, obj()), std::nullopt);
        EXPECT_FALSE(ConverterJ::nothingRaw(env, obj())); // a raw reference: the caller decides
        try {
            ConverterJ::nothing(env, obj());
            FAIL() << "a null for std::string must not pass";
        } catch (const IllegalState &e) {
            EXPECT_TRUE(contains(e.what(), "returned null")) << e.what();
            EXPECT_TRUE(contains(e.what(), "std::optional")) << e.what();
            EXPECT_TRUE(contains(e.what(), "nothing")) << e.what();
        }
    }

    // ---- fromJava / toJava by hand ----

    TEST_F(ConvertTest, FromJavaAndToJavaByHand) {
        auto java = toJava(env, std::string("кадр"));
        static_assert(std::is_same_v<decltype(java), LocalRef<jstring> >);
        EXPECT_EQ(fromJava<std::string>(env, java.get()), "кадр");
        EXPECT_EQ(fromJava<std::u16string>(env, java.get()), u"кадр");

        auto literal = toJava(env, "literal");
        EXPECT_EQ(fromJava<std::string>(env, literal.get()), "literal");

        EXPECT_EQ(toJava(env, 5), 5);
        EXPECT_EQ(toJava(env, true), JNI_TRUE);
        EXPECT_TRUE(fromJava<bool>(env, JNI_TRUE));
        EXPECT_FALSE(fromJava<bool>(env, JNI_FALSE));

        EXPECT_EQ(fromJava<std::optional<std::string> >(env, nullptr), std::nullopt);
        auto boxed = toJava(env, std::optional<jint>(7));
        static_assert(std::is_same_v<decltype(boxed), LocalRef<jobject> >);
        EXPECT_EQ(fromJava<std::optional<jint> >(env, boxed.get()), std::optional<jint>(7));
        EXPECT_FALSE(toJava(env, std::optional<jint>()));
    }

    TEST_F(ConvertTest, ANullThatCanNotBeHeldIsAnIllegalArgument) {
        EXPECT_THROW(fromJava<std::string>(env, nullptr), IllegalArgument);
        try {
            fromJava<std::string>(env, nullptr);
        } catch (const IllegalArgument &e) {
            EXPECT_TRUE(contains(e.what(), "null")) << e.what();
            EXPECT_EQ(e.line(), 0); // no place in jnirt
        }
    }

    TEST_F(ConvertTest, RawTypesStillPassThroughUntouched) {
        LocalRef<jstring> text = toJava(env, std::string("raw"));
        LocalRef<jstring> back = ConverterJ::echoStringRaw(env, obj(), text.get());
        EXPECT_EQ(fromJava<std::string>(env, back.get()), "raw");
    }
} // namespace

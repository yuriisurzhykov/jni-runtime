// Arrays of primitives and direct buffers (ADR-0008 §5, §6) at work: natives written with std::vector and raw arrays,
// and Java called with C++ types, against a real JVM (-Xcheck:jni). Both layers meet in each test: class_cache converts
// the arguments, the registered native converts them back, and the result takes the same way home.

#include <jnirt/runtime/convert.h>
#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/runtime/signature.h>
#include <jnirt/support/assert.h>
#include <jnirt/support/error.h>

#include "class_cache_internal.h"
#include "critical.h"
#include "ref_internal.h"
#include "registry_internal.h"
#include "test_jvm.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace jnirt;

namespace {
    using std::string_view;
    using Ints = std::vector<jint>;

    // ---- compile time ----

    static_assert(string_view(descriptor<std::vector<jint> >) == "[I");
    static_assert(string_view(descriptor<std::vector<jlong> >) == "[J");
    static_assert(string_view(descriptor<std::vector<jfloat> >) == "[F");
    static_assert(string_view(descriptor<std::vector<jdouble> >) == "[D");
    static_assert(string_view(descriptor<std::vector<jshort> >) == "[S");
    static_assert(string_view(descriptor<std::vector<jchar> >) == "[C");
    static_assert(string_view(descriptor<std::vector<jbyte> >) == "[B");
    static_assert(string_view(descriptor<std::vector<std::byte> >) == "[B");
    static_assert(string_view(descriptor<std::vector<bool> >) == "[Z");
    static_assert(string_view(descriptor<const std::vector<jint> &>) == "[I");
    static_assert(string_view(descriptor<std::optional<std::vector<jint> > >) == "[I");
    static_assert(string_view(descriptor<DirectBuffer>) == "Ljava/nio/ByteBuffer;");
    static_assert(string_view(descriptor<std::optional<DirectBuffer> >) == "Ljava/nio/ByteBuffer;");
    static_assert(string_view(signature<std::vector<jint>(std::vector<jint>, DirectBuffer)>) ==
                  "([ILjava/nio/ByteBuffer;)[I");

    static_assert(std::is_same_v<detail::JniOf<std::vector<jint> >, jintArray>);
    static_assert(std::is_same_v<detail::JniOf<std::vector<std::byte> >, jbyteArray>);
    static_assert(std::is_same_v<detail::JniOf<std::vector<bool> >, jbooleanArray>);
    static_assert(std::is_same_v<detail::JniOf<DirectBuffer>, jobject>);
    static_assert(detail::rejectsNull<std::vector<jint> >);
    static_assert(!detail::rejectsNull<std::optional<std::vector<jint> > >);
    static_assert(detail::BorrowFlag<detail::RowOf<DirectBuffer> >::value);
    static_assert(detail::BorrowFlag<detail::RowOf<std::optional<DirectBuffer> > >::value);
    static_assert(!detail::BorrowFlag<detail::RowOf<std::vector<jint> > >::value);

    // ---- the class under test ----

    std::vector<jint> doubled(JNIEnv *, jobject, std::vector<jint> values) {
        for (jint &value: values) value *= 2;
        return values;
    }

    std::vector<std::byte> echoBytes(JNIEnv *, jobject, const std::vector<std::byte> &values) { return values; }

    std::vector<jbyte> echoSignedBytes(JNIEnv *, jobject, const std::vector<jbyte> &values) { return values; }

    std::vector<bool> flip(JNIEnv *, jobject, const std::vector<bool> &flags) {
        std::vector<bool> result = flags;
        result.flip();
        return result;
    }

    std::vector<jchar> echoChars(JNIEnv *, jobject, const std::vector<jchar> &values) { return values; }

    std::vector<jshort> echoShorts(JNIEnv *, jobject, const std::vector<jshort> &values) { return values; }

    std::vector<jlong> echoLongs(JNIEnv *, jobject, const std::vector<jlong> &values) { return values; }

    std::vector<jfloat> echoFloats(JNIEnv *, jobject, const std::vector<jfloat> &values) { return values; }

    std::vector<jdouble> echoDoubles(JNIEnv *, jobject, const std::vector<jdouble> &values) { return values; }

    std::optional<std::vector<jint> > echoOptInts(JNIEnv *, jobject, const std::optional<std::vector<jint> > &values) {
        return values;
    }

    // pieces
    std::vector<jint> slice(JNIEnv *env, jobject, jintArray values, jint start, jint count) {
        std::vector<jint> piece(static_cast<std::size_t>(count));
        readArray(env, values, start, piece.data(), count);
        return piece;
    }

    void fillPiece(JNIEnv *env, jobject, jintArray target, jint start, jint count, jint value) {
        std::vector<jint> piece(static_cast<std::size_t>(count), value);
        writeArray(env, target, start, piece.data(), count);
    }

    // A uint8_t buffer is fine here: the handle says it is a ByteArray.
    void fillBytes(JNIEnv *env, jobject, jbyteArray target, jint value) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(arrayLength(env, target)),
                                        static_cast<std::uint8_t>(value));
        writeArray(env, target, 0, bytes.data(), static_cast<jsize>(bytes.size()));
    }

    std::vector<jint> sliceOutside(JNIEnv *env, jobject, jintArray values) {
        std::vector<jint> piece(10);
        readArray(env, values, 0, piece.data(), 10);
        return piece;
    }

    jint lengthOf(JNIEnv *env, jobject, jintArray values) { return arrayLength(env, values); }

    // no copy
    jlong sum(JNIEnv *env, jobject, jintArray values) {
        return withCriticalRead(env, values, [](const jint *data, std::size_t size) {
            jlong total = 0;
            for (std::size_t i = 0; i < size; ++i) total += data[i];
            return total;
        });
    }

    void negate(JNIEnv *env, jobject, jintArray values) {
        withCritical(env, values, [](jint *data, std::size_t size) {
            for (std::size_t i = 0; i < size; ++i) data[i] = -data[i];
        });
    }

    void negateOneThenThrow(JNIEnv *env, jobject, jintArray values) {
        withCritical(env, values, [](jint *data, std::size_t size) {
            if (size > 0) data[0] = -data[0];
            JNIRT_CHECK(false, "stopped inside the critical region");
        });
    }

    jlong sumBytes(JNIEnv *env, jobject, jbyteArray values) {
        return withCriticalRead(env, values, [](const jbyte *data, std::size_t size) {
            return std::accumulate(data, data + size, jlong{0});
        });
    }

    jint sizeSeen(JNIEnv *env, jobject, jintArray values) {
        return withCriticalRead(env, values, [](const jint *, std::size_t size) { return static_cast<jint>(size); });
    }

    // direct buffer
    jint capacityOf(JNIEnv *, jobject, DirectBuffer buffer) { return static_cast<jint>(buffer.size()); }

    void fill(JNIEnv *, jobject, DirectBuffer buffer, jint value) {
        std::memset(buffer.data(), value, buffer.size());
    }

    jint capacityOrMinusOne(JNIEnv *, jobject, std::optional<DirectBuffer> buffer) {
        return buffer ? static_cast<jint>(buffer->size()) : -1;
    }

    struct ByteBufferJ : JavaClass<ByteBufferJ> {
        static constexpr const char *name = "java/nio/ByteBuffer";
    };

    struct ArrayConverterJ : JavaClass<ArrayConverterJ> {
        static constexpr const char *name = "jnirt/test/ArrayConverter";

        static inline Constructor<ArrayConverterJ, void()> create;

        // The natives, described with C++ types ...
        static inline Method<ArrayConverterJ, std::vector<jint>(std::vector<jint>)> doubled{"doubled"};
        static inline Method<ArrayConverterJ, std::vector<std::byte>(std::vector<std::byte>)> echoBytes{"echoBytes"};
        static inline Method<ArrayConverterJ, std::vector<jbyte>(std::vector<jbyte>)> echoSignedBytes{
            "echoSignedBytes"
        };
        static inline Method<ArrayConverterJ, std::vector<bool>(std::vector<bool>)> flip{"flip"};
        static inline Method<ArrayConverterJ, std::vector<jchar>(std::vector<jchar>)> echoChars{"echoChars"};
        static inline Method<ArrayConverterJ, std::vector<jshort>(std::vector<jshort>)> echoShorts{"echoShorts"};
        static inline Method<ArrayConverterJ, std::vector<jlong>(std::vector<jlong>)> echoLongs{"echoLongs"};
        static inline Method<ArrayConverterJ, std::vector<jfloat>(std::vector<jfloat>)> echoFloats{"echoFloats"};
        static inline Method<ArrayConverterJ, std::vector<jdouble>(std::vector<jdouble>)> echoDoubles{"echoDoubles"};
        static inline Method<ArrayConverterJ, std::optional<std::vector<jint> >(std::optional<std::vector<jint> >)>
        echoOptInts{"echoOptInts"};
        static inline Method<ArrayConverterJ, std::vector<jint>(jintArray, jint, jint)> sliceRaw{"slice"};
        static inline Method<ArrayConverterJ, void(jintArray, jint, jint, jint)> fillPieceRaw{"fillPiece"};
        static inline Method<ArrayConverterJ, jlong(jintArray)> sum{"sum"};
        static inline Method<ArrayConverterJ, jlong(jbyteArray)> sumBytes{"sumBytes"};
        static inline Method<ArrayConverterJ, jint(jintArray)> sizeSeen{"sizeSeen"};
        static inline Method<ArrayConverterJ, jint(DirectBuffer)> capacityOf{"capacityOf"};
        static inline Method<ArrayConverterJ, jint(ByteBufferJ)> capacityOrMinusOne{"capacityOrMinusOne"};

        // ... and with raw JNI types, to hold the arrays themselves.
        static inline Method<ArrayConverterJ, jintArray(jintArray)> doubledRaw{"doubled"};
        static inline Method<ArrayConverterJ, void(jintArray)> negate{"negate"};
        static inline Method<ArrayConverterJ, void(jintArray)> negateOneThenThrow{"negateOneThenThrow"};
        static inline Method<ArrayConverterJ, jint(jintArray)> lengthOfRaw{"lengthOf"};
        static inline Method<ArrayConverterJ, jintArray(jintArray)> sliceOutside{"sliceOutside"};
        static inline Method<ArrayConverterJ, void(jbyteArray, jint)> fillBytes{"fillBytes"};
        static inline Method<ArrayConverterJ, void(ByteBufferJ, jint)> fillRaw{"fill"};
        static inline Method<ArrayConverterJ, jint(ByteBufferJ)> capacityOfRaw{"capacityOf"};

        // Java members that native code calls.
        static inline Field<ArrayConverterJ, std::vector<jint> > ints{"ints"};
        static inline Field<ArrayConverterJ, std::vector<std::byte> > bytes{"bytes"};
        static inline Field<ArrayConverterJ, std::optional<std::vector<jint> > > nothing{"nothing"};
        static inline Field<ArrayConverterJ, ByteBufferJ> buffer{"buffer"};
        static inline Method<ArrayConverterJ, jint(std::vector<jint>)> sumOf{"sumOf"};
        static inline Method<ArrayConverterJ, std::vector<jint>(jint)> makeInts{"makeInts"};
        static inline Method<ArrayConverterJ, std::vector<jint>()> nullInts{"nullInts"};
        static inline Method<ArrayConverterJ, std::optional<std::vector<jint> >()> nullIntsOpt{"nullInts"};
        static inline Method<ArrayConverterJ, ByteBufferJ(jint)> direct{"direct"};
        static inline Method<ArrayConverterJ, ByteBufferJ(jint)> heap{"heap"};
    };

    void registerArrayConverter(Registry &r) {
        r.add<ArrayConverterJ>()
                .native<&doubled>("doubled")
                .native<&echoBytes>("echoBytes")
                .native<&echoSignedBytes>("echoSignedBytes")
                .native<&flip>("flip")
                .native<&echoChars>("echoChars")
                .native<&echoShorts>("echoShorts")
                .native<&echoLongs>("echoLongs")
                .native<&echoFloats>("echoFloats")
                .native<&echoDoubles>("echoDoubles")
                .native<&echoOptInts>("echoOptInts")
                .native<&slice>("slice")
                .native<&fillPiece>("fillPiece")
                .native<&fillBytes>("fillBytes")
                .native<&sliceOutside>("sliceOutside")
                .native<&lengthOf>("lengthOf")
                .native<&sum>("sum")
                .native<&negate>("negate")
                .native<&negateOneThenThrow>("negateOneThenThrow")
                .native<&sumBytes>("sumBytes")
                .native<&sizeSeen>("sizeSeen")
                .native<&capacityOf>("capacityOf")
                .native<&fill>("fill")
                .native<&capacityOrMinusOne>("capacityOrMinusOne");
    }

    class ConvertArrayTest : public ::testing::Test {
    protected:
        void SetUp() override {
            env = jnirt::env();
            Registry registry;
            registerArrayConverter(registry);
            auto problems = detail::startup(env, registry, {&detail::classInfo<ArrayConverterJ>()});
            ASSERT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0]);
            self = ArrayConverterJ::create(env);
            localRefsBefore = detail::liveLocalRefsOnThisThread();
        }

        // Nothing leaked, nothing pending.
        void TearDown() override {
            EXPECT_FALSE(env->ExceptionCheck());
            EXPECT_EQ(detail::liveLocalRefsOnThisThread(), localRefsBefore) << "local references leaked";
        }

        jobject obj() const { return self.get(); }

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

        LocalRef<jintArray> intArray(const std::vector<jint> &values) {
            return Convert<std::vector<jint> >::toJava(env, values);
        }

        JNIEnv *env = nullptr;
        LocalRef<jobject> self;
        int localRefsBefore = 0;
    };

    // ---- copies ----

    TEST_F(ConvertArrayTest, AnIntArrayGoesThereAndBack) {
        EXPECT_EQ(ArrayConverterJ::doubled(env, obj(), Ints{1, 2, 3}), (Ints{2, 4, 6}));
        EXPECT_EQ(ArrayConverterJ::doubled(env, obj(), Ints{}), Ints{});
        EXPECT_EQ(ArrayConverterJ::doubled(env, obj(), Ints{INT32_MAX / 2, INT32_MIN / 2}),
                  (Ints{INT32_MAX - 1, INT32_MIN}));
    }

    TEST_F(ConvertArrayTest, EveryElementTypeCrossesUnchanged) {
        using Bytes = std::vector<std::byte>;
        const Bytes bytes{std::byte{0}, std::byte{127}, std::byte{128}, std::byte{255}};
        EXPECT_EQ(ArrayConverterJ::echoBytes(env, obj(), bytes), bytes);
        const std::vector<jbyte> signedBytes{-128, -1, 0, 127};
        EXPECT_EQ(ArrayConverterJ::echoSignedBytes(env, obj(), signedBytes), signedBytes);
        EXPECT_EQ(ArrayConverterJ::flip(env, obj(), std::vector<bool>{true, false, true, true}),
                  (std::vector<bool>{false, true, false, false}));
        const std::vector<jchar> chars{u'a', u'я', 0xD83D, 0xDE00}; // an unpaired surrogate is just a number here
        EXPECT_EQ(ArrayConverterJ::echoChars(env, obj(), chars), chars);
        const std::vector<jshort> shorts{-32768, 0, 32767};
        EXPECT_EQ(ArrayConverterJ::echoShorts(env, obj(), shorts), shorts);
        const std::vector<jlong> longs{INT64_MIN, 0, INT64_MAX};
        EXPECT_EQ(ArrayConverterJ::echoLongs(env, obj(), longs), longs);
        const std::vector<jfloat> floats{1.5f, -0.25f, 1e30f};
        EXPECT_EQ(ArrayConverterJ::echoFloats(env, obj(), floats), floats);
        const std::vector<jdouble> doubles{1.5, -0.25, 1e300};
        EXPECT_EQ(ArrayConverterJ::echoDoubles(env, obj(), doubles), doubles);
    }

    TEST_F(ConvertArrayTest, ABigArrayIsCopiedWhole) {
        std::vector<jint> big(3'000'000);
        std::iota(big.begin(), big.end(), 0);
        auto result = ArrayConverterJ::doubled(env, obj(), big);
        ASSERT_EQ(result.size(), big.size());
        EXPECT_EQ(result.front(), 0);
        EXPECT_EQ(result[1'234'567], 2 * 1'234'567);
        EXPECT_EQ(result.back(), 2 * 2'999'999);
    }

    TEST_F(ConvertArrayTest, ABooleanArrayKeepsEveryFlag) {
        std::vector<bool> flags;
        for (int i = 0; i < 1000; ++i) flags.push_back(i % 3 == 0);
        auto flipped = ArrayConverterJ::flip(env, obj(), flags);
        ASSERT_EQ(flipped.size(), flags.size());
        for (std::size_t i = 0; i < flags.size(); ++i) ASSERT_NE(flipped[i], flags[i]) << i;
    }

    TEST_F(ConvertArrayTest, AnOptionalArrayIsAnArrayOrNull) {
        using Opt = std::optional<Ints>;
        EXPECT_EQ(ArrayConverterJ::echoOptInts(env, obj(), std::nullopt), std::nullopt);
        EXPECT_EQ(ArrayConverterJ::echoOptInts(env, obj(), Opt(Ints{})), Opt(Ints{}));
        EXPECT_EQ(ArrayConverterJ::echoOptInts(env, obj(), Opt(Ints{4, 5})), Opt(Ints{4, 5}));
    }

    TEST_F(ConvertArrayTest, ANullArrayIsAnErrorWhereTheTypeHasNoNull) {
        std::string failure = javaFailure([&] { ArrayConverterJ::doubledRaw(env, obj(), nullptr); });
        EXPECT_TRUE(contains(failure, "java.lang.IllegalArgumentException")) << failure;
        EXPECT_TRUE(contains(failure, "argument 1 is null")) << failure;
        EXPECT_TRUE(contains(failure, "[I")) << failure;
        EXPECT_FALSE(contains(failure, "convert")) << "no place in jnirt: " << failure;
    }

    TEST_F(ConvertArrayTest, ANullResultIsAnErrorOrAnOptional) {
        EXPECT_EQ(ArrayConverterJ::nullIntsOpt(env, obj()), std::nullopt);
        try {
            ArrayConverterJ::nullInts(env, obj());
            FAIL() << "a null for std::vector must not pass";
        } catch (const IllegalState &e) {
            EXPECT_TRUE(contains(e.what(), "returned null")) << e.what();
            EXPECT_TRUE(contains(e.what(), "std::optional")) << e.what();
        }
    }

    // ---- pieces ----

    TEST_F(ConvertArrayTest, APieceIsReadAndWritten) {
        auto array = intArray({10, 20, 30, 40, 50});
        EXPECT_EQ(ArrayConverterJ::sliceRaw(env, obj(), array.get(), 1, 3), (Ints{20, 30, 40}));

        ArrayConverterJ::fillPieceRaw(env, obj(), array.get(), 3, 2, 7);
        jint now[5];
        readArray(env, array.get(), 0, now, 5);
        EXPECT_EQ(Ints(now, now + 5), (Ints{10, 20, 30, 7, 7}));

        const jint nine[2] = {9, 9};
        writeArray(env, array.get(), 0, nine, 2);
        readArray(env, array.get(), 0, now, 5);
        EXPECT_EQ(Ints(now, now + 5), (Ints{9, 9, 30, 7, 7}));
    }

    TEST_F(ConvertArrayTest, AByteBufferOfUint8IsFineForAByteArray) {
        LocalRef<jbyteArray> array(env, env->NewByteArray(4));
        ArrayConverterJ::fillBytes(env, obj(), array.get(), 200);
        jbyte read[4];
        readArray(env, array.get(), 0, read, 4);
        EXPECT_EQ(read[0], static_cast<jbyte>(200));
        std::uint8_t unsignedRead[4];
        readArray(env, array.get(), 0, unsignedRead, 4);
        EXPECT_EQ(unsignedRead[3], 200);
        std::byte bytes[4];
        readArray(env, array.get(), 0, bytes, 4);
        EXPECT_EQ(bytes[1], std::byte{200});
    }

    TEST_F(ConvertArrayTest, ARangeOutsideTheArrayIsTheJvmsException) {
        auto array = intArray({1, 2, 3});
        std::string failure = javaFailure([&] { ArrayConverterJ::sliceOutside(env, obj(), array.get()); });
        EXPECT_TRUE(contains(failure, "ArrayIndexOutOfBoundsException")) << failure;

        jint buffer[2];
        EXPECT_THROW(readArray(env, array.get(), 2, buffer, 2), JavaException);
        EXPECT_THROW(writeArray(env, array.get(), -1, buffer, 1), JavaException);
        EXPECT_FALSE(env->ExceptionCheck()) << "the exception is thrown as JavaException and cleared";
    }

    TEST_F(ConvertArrayTest, ALengthAndANullArray) {
        auto array = intArray({1, 2, 3, 4});
        EXPECT_EQ(arrayLength(env, array.get()), 4);
        EXPECT_EQ(ArrayConverterJ::lengthOfRaw(env, obj(), array.get()), 4);
        EXPECT_THROW(arrayLength(env, nullptr), IllegalArgument);
        std::string failure = javaFailure([&] { ArrayConverterJ::lengthOfRaw(env, obj(), nullptr); });
        EXPECT_TRUE(contains(failure, "IllegalArgumentException")) << failure;
        EXPECT_TRUE(contains(failure, "Java array is null")) << failure;
    }

    // ---- withCritical ----

    TEST_F(ConvertArrayTest, ASumWithoutACopy) {
        std::vector<jint> numbers(1'000'000, 3);
        auto array = intArray(numbers);
        EXPECT_EQ(ArrayConverterJ::sum(env, obj(), array.get()), 3'000'000);
        EXPECT_EQ(ArrayConverterJ::sizeSeen(env, obj(), array.get()), 1'000'000);

        LocalRef<jbyteArray> bytes(env, env->NewByteArray(3));
        const jbyte three[3] = {-1, 2, 100};
        env->SetByteArrayRegion(bytes.get(), 0, 3, three);
        EXPECT_EQ(ArrayConverterJ::sumBytes(env, obj(), bytes.get()), 101);
    }

    TEST_F(ConvertArrayTest, WhatTheLambdaWritesIsInTheArray) {
        auto array = intArray({1, -2, 3});
        ArrayConverterJ::negate(env, obj(), array.get());
        jint now[3];
        readArray(env, array.get(), 0, now, 3);
        EXPECT_EQ(std::vector<jint>(now, now + 3), (Ints{-1, 2, -3}));
    }

    TEST_F(ConvertArrayTest, AnEmptyArrayHasSizeZero) {
        auto array = intArray({});
        EXPECT_EQ(ArrayConverterJ::sizeSeen(env, obj(), array.get()), 0);
        ArrayConverterJ::negate(env, obj(), array.get());
    }

    TEST_F(ConvertArrayTest, AnExceptionInsideReleasesTheArrayAndGoesToJava) {
        auto array = intArray({5, 6});
        std::string failure = javaFailure([&] { ArrayConverterJ::negateOneThenThrow(env, obj(), array.get()); });
        EXPECT_TRUE(contains(failure, "java.lang.IllegalStateException")) << failure;
        EXPECT_TRUE(contains(failure, "stopped inside the critical region")) << failure;
        // The array is released (-Xcheck:jni would complain about the next JNI call otherwise), and what was
        // written before the exception stays.
        jint now[2];
        readArray(env, array.get(), 0, now, 2);
        EXPECT_EQ(now[0], -5);
        EXPECT_EQ(now[1], 6);
    }

    TEST_F(ConvertArrayTest, TheCriticalFlagIsSetOnlyInsideTheLambda) {
        auto array = intArray({1});
        EXPECT_FALSE(detail::insideCritical());
        withCriticalRead(env, array.get(), [](const jint *, std::size_t) { EXPECT_TRUE(detail::insideCritical()); });
        EXPECT_FALSE(detail::insideCritical());
        try {
            withCritical(env, array.get(), [](jint *, std::size_t) { throw std::runtime_error("inside"); });
        } catch (const std::runtime_error &) {
        }
        EXPECT_FALSE(detail::insideCritical()) << "an exception must not leave the thread 'inside critical'";
    }

    TEST_F(ConvertArrayTest, AJniCallInsideTheLambdaIsCaughtInDebug) {
#ifdef NDEBUG
        GTEST_SKIP() << "the check exists in debug builds only";
#else
        auto array = intArray({1});
        EXPECT_DEATH(withCritical(env, array.get(), [](jint *, std::size_t) { jnirt::env(); }),
                     "jnirt::env\\(\\) was called inside withCritical");
        EXPECT_DEATH(withCritical(env, array.get(), [&](jint *, std::size_t) { ArrayConverterJ::makeInts(env, obj(), 1); }),
                     "was called inside withCritical");
        EXPECT_DEATH(withCriticalRead(env, array.get(), [&](const jint *, std::size_t) {
                         withCriticalRead(env, array.get(), [](const jint *, std::size_t) {});
                     }),
                     "withCritical was called inside withCritical");
#endif
    }

    // ---- direct buffer ----

    TEST_F(ConvertArrayTest, ADirectBufferIsMemoryOfJava) {
        auto buffer = ArrayConverterJ::direct(env, obj(), 64);
        EXPECT_EQ(ArrayConverterJ::capacityOfRaw(env, obj(), buffer.get()), 64);
        ArrayConverterJ::fillRaw(env, obj(), buffer.get(), 0x5A);
        auto memory = fromJava<DirectBuffer>(env, buffer.get());
        ASSERT_EQ(memory.size(), 64u);
        EXPECT_EQ(std::to_integer<int>(memory.data()[0]), 0x5A);
        EXPECT_EQ(std::to_integer<int>(memory.data()[63]), 0x5A);
        EXPECT_EQ(memory.as<std::uint8_t>()[10], 0x5A);
        memory.data()[5] = std::byte{1}; // Java sees what C++ writes: the same memory
        EXPECT_EQ(ArrayConverterJ::capacityOfRaw(env, obj(), buffer.get()), 64);
    }

    TEST_F(ConvertArrayTest, ACapacityIsTheCapacityNotTheLimit) {
        auto buffer = ArrayConverterJ::direct(env, obj(), 32);
        EXPECT_EQ(ArrayConverterJ::capacityOfRaw(env, obj(), buffer.get()), 32);
        EXPECT_EQ(fromJava<DirectBuffer>(env, buffer.get()).size(), 32u);
    }

    TEST_F(ConvertArrayTest, ABufferThatIsNotDirectIsAnError) {
        auto buffer = ArrayConverterJ::heap(env, obj(), 16);
        std::string failure = javaFailure([&] { ArrayConverterJ::capacityOfRaw(env, obj(), buffer.get()); });
        EXPECT_TRUE(contains(failure, "IllegalArgumentException")) << failure;
        EXPECT_TRUE(contains(failure, "not direct")) << failure;
        EXPECT_THROW(fromJava<DirectBuffer>(env, buffer.get()), IllegalArgument);
    }

    TEST_F(ConvertArrayTest, ANullBufferIsAnErrorUnlessItIsOptional) {
        std::string failure = javaFailure([&] { ArrayConverterJ::capacityOfRaw(env, obj(), nullptr); });
        EXPECT_TRUE(contains(failure, "argument 1 is null")) << failure;
        EXPECT_TRUE(contains(failure, "Ljava/nio/ByteBuffer;")) << failure;
        EXPECT_EQ(ArrayConverterJ::capacityOrMinusOne(env, obj(), nullptr), -1);
        auto buffer = ArrayConverterJ::direct(env, obj(), 8);
        EXPECT_EQ(ArrayConverterJ::capacityOrMinusOne(env, obj(), buffer.get()), 8);
    }

    // ---- fields and results ----

    TEST_F(ConvertArrayTest, FieldsHoldArrays) {
        EXPECT_EQ(ArrayConverterJ::ints.get(env, obj()), (Ints{1, 2, 3}));
        ArrayConverterJ::ints.set(env, obj(), Ints{9, 8});
        EXPECT_EQ(ArrayConverterJ::ints.get(env, obj()), (Ints{9, 8}));
        EXPECT_EQ(ArrayConverterJ::bytes.get(env, obj()),
                  (std::vector<std::byte>{std::byte{10}, std::byte{20}, std::byte{30}}));
        EXPECT_EQ(ArrayConverterJ::nothing.get(env, obj()), std::nullopt);
        ArrayConverterJ::nothing.set(env, obj(), Ints{1});
        EXPECT_EQ(ArrayConverterJ::nothing.get(env, obj()), std::optional<std::vector<jint> >(Ints{1}));
        ArrayConverterJ::nothing.set(env, obj(), std::nullopt);
        EXPECT_EQ(ArrayConverterJ::nothing.get(env, obj()), std::nullopt);
    }

    TEST_F(ConvertArrayTest, JavaGivesAndTakesVectors) {
        EXPECT_EQ(ArrayConverterJ::makeInts(env, obj(), 5), (Ints{0, 1, 4, 9, 16}));
        EXPECT_EQ(ArrayConverterJ::sumOf(env, obj(), Ints{1, 2, 3, 4}), 10);
    }

    TEST_F(ConvertArrayTest, AFieldDirectBufferIsReadThroughTheReference) {
        auto buffer = ArrayConverterJ::buffer.get(env, obj()); // LocalRef: the buffer object is held while we use it
        EXPECT_EQ(fromJava<DirectBuffer>(env, buffer.get()).size(), 16u);
    }

    // ---- the references stay flat ----

    TEST_F(ConvertArrayTest, ManyCallsDoNotPileUpLocalReferences) {
        for (int i = 0; i < 20'000; ++i) {
            auto result = ArrayConverterJ::doubled(env, obj(), Ints{i, i + 1});
            ASSERT_EQ(result[1], 2 * (i + 1));
        }
        // TearDown compares the counter with the one from SetUp.
    }
} // namespace

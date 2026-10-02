// Objects and collections (ADR-0008 §7) at work: std::vector as List, std::map as Map, your own type (Frame) with a row
// of its own, and Array<C>. Natives written with C++ types, and Java called with C++ types, against a real JVM
// (-Xcheck:jni). Both layers meet in each test.

#include <jnirt/runtime/convert.h>
#include <jnirt/runtime/env.h>
#include <jnirt/runtime/registry.h>
#include <jnirt/runtime/signature.h>
#include <jnirt/support/error.h>

#include "class_cache_internal.h"
#include "ref_internal.h"
#include "registry_internal.h"
#include "test_jvm.h"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using namespace jnirt;

namespace {
    // ---- your own type: Frame, with a row (this is what a library writes in bindings/) ----

    struct Frame {
        jint index = 0;
        std::string label;

        bool operator==(const Frame &other) const { return index == other.index && label == other.label; }
    };

    struct FrameJ : JavaClass<FrameJ> {
        static constexpr const char *name = "jnirt/test/Frame";

        static inline Constructor<FrameJ, void(jint, std::string)> create;
        static inline Field<FrameJ, jint> index{"index"};
        static inline Field<FrameJ, std::string> label{"label"};
    };

    // java.lang.String as an element type that counts the local references while it is converted.
    struct StringJ : JavaClass<StringJ> {
        static constexpr const char *name = "java/lang/String";
    };

    struct Probe {
        std::string text;
    };

    int gPeakLocals = 0;

    void noteLocals() { gPeakLocals = std::max(gPeakLocals, detail::liveLocalRefsOnThisThread()); }
} // namespace

namespace jnirt {
    template<>
    struct Convert<Frame> : ConvertObject<Frame, FrameJ> {
        static Frame fromJava(JNIEnv *env, jobject frame) {
            return Frame{FrameJ::index.get(env, frame), FrameJ::label.get(env, frame)};
        }

        static LocalRef<jobject> toJava(JNIEnv *env, const Frame &frame) {
            return FrameJ::create(env, frame.index, frame.label);
        }
    };

    template<>
    struct Convert<Probe> : ConvertObject<Probe, StringJ> {
        static Probe fromJava(JNIEnv *env, jobject text) {
            noteLocals();
            return Probe{jnirt::fromJava<std::string>(env, static_cast<jstring>(text))};
        }

        static LocalRef<jobject> toJava(JNIEnv *env, const Probe &probe) {
            noteLocals();
            LocalRef<jstring> text = jnirt::toJava(env, probe.text);
            return LocalRef<jobject>(env, text.release());
        }
    };
} // namespace jnirt

namespace {
    using std::string_view;
    using Strings = std::vector<std::string>;
    using OptStrings = std::vector<std::optional<std::string> >;
    using Counts = std::map<std::string, jint>;

    // ---- compile time ----

    static_assert(string_view(descriptor<Strings>) == "Ljava/util/List;");
    static_assert(string_view(descriptor<OptStrings>) == "Ljava/util/List;");
    static_assert(string_view(descriptor<std::vector<Frame> >) == "Ljava/util/List;");
    static_assert(string_view(descriptor<std::vector<std::vector<jint> > >) == "Ljava/util/List;");
    static_assert(string_view(descriptor<Counts>) == "Ljava/util/Map;");
    static_assert(string_view(descriptor<std::unordered_map<std::string, Frame> >) == "Ljava/util/Map;");
    static_assert(string_view(descriptor<std::optional<Strings> >) == "Ljava/util/List;");
    static_assert(string_view(descriptor<Frame>) == "Ljnirt/test/Frame;");
    static_assert(string_view(descriptor<std::optional<Frame> >) == "Ljnirt/test/Frame;");
    static_assert(string_view(descriptor<Array<FrameJ> >) == "[Ljnirt/test/Frame;");
    static_assert(string_view(descriptor<std::optional<Array<FrameJ> > >) == "[Ljnirt/test/Frame;");
    static_assert(string_view(signature<Strings(Counts, Array<FrameJ>, Frame)>) ==
                  "(Ljava/util/Map;[Ljnirt/test/Frame;Ljnirt/test/Frame;)Ljava/util/List;");

    static_assert(std::is_same_v<detail::JniOf<Strings>, jobject>);
    static_assert(std::is_same_v<detail::JniOf<Array<FrameJ> >, jobjectArray>);
    static_assert(std::is_same_v<detail::JniOf<Frame>, jobject>);
    static_assert(std::is_same_v<detail::ValueOf<const Frame &>, Frame>);
    static_assert(detail::rejectsNull<Strings>);
    static_assert(detail::rejectsNull<Counts>);
    static_assert(detail::rejectsNull<Frame>);
    static_assert(detail::rejectsNull<Array<FrameJ> >);
    static_assert(!detail::rejectsNull<std::optional<Frame> >);

    // ---- the class under test ----

    Strings echoStrings(JNIEnv *, jobject, const Strings &items) { return items; }

    OptStrings echoOptStrings(JNIEnv *, jobject, const OptStrings &items) { return items; }

    std::vector<Frame> reversed(JNIEnv *, jobject, std::vector<Frame> frames) {
        std::reverse(frames.begin(), frames.end());
        return frames;
    }

    std::vector<Strings> echoNested(JNIEnv *, jobject, const std::vector<Strings> &nested) { return nested; }

    std::vector<std::optional<jint> > echoBoxed(JNIEnv *, jobject, const std::vector<std::optional<jint> > &numbers) {
        return numbers;
    }

    std::vector<std::vector<jint> > echoArrays(JNIEnv *, jobject, const std::vector<std::vector<jint> > &arrays) {
        return arrays;
    }

    jint totalLength(JNIEnv *, jobject, const Strings &items) {
        jint total = 0;
        for (const std::string &item: items) total += static_cast<jint>(item.size());
        return total;
    }

    Strings range(JNIEnv *, jobject, jint count) {
        Strings result;
        for (jint i = 0; i < count; ++i) result.push_back("item " + std::to_string(i));
        return result;
    }

    std::vector<Probe> echoProbes(JNIEnv *, jobject, const std::vector<Probe> &items) { return items; }

    Counts echoCounts(JNIEnv *, jobject, const Counts &counts) { return counts; }

    std::unordered_map<std::string, Frame> echoFrameMap(JNIEnv *, jobject,
                                                         const std::unordered_map<std::string, Frame> &frames) {
        return frames;
    }

    std::map<jint, std::string> echoIntKeys(JNIEnv *, jobject, const std::map<jint, std::string> &names) {
        return names;
    }

    std::map<std::string, std::optional<std::string> > echoOptValues(
        JNIEnv *, jobject, const std::map<std::string, std::optional<std::string> > &map) {
        return map;
    }

    Counts numbered(JNIEnv *, jobject, jint count) {
        Counts result;
        for (jint i = 0; i < count; ++i) result["k" + std::to_string(i)] = i;
        return result;
    }

    Frame echoFrame(JNIEnv *, jobject, Frame frame) { return frame; }

    std::optional<Frame> maybeFrame(JNIEnv *, jobject, std::optional<Frame> frame) { return frame; }

    Array<FrameJ> firstTwo(JNIEnv *env, jobject, const Array<FrameJ> &frames) {
        Array<FrameJ> result = newArray<FrameJ>(env, 2);
        for (jsize i = 0; i < 2; ++i) result.set(env, i, frames.get(env, i).get());
        return result;
    }

    Array<FrameJ> makeFrameArray(JNIEnv *env, jobject, jint count) {
        Array<FrameJ> result = newArray<FrameJ>(env, count);
        for (jint i = 0; i < count; ++i) {
            LocalRef<jobject> frame = FrameJ::create(env, i, "frame " + std::to_string(i));
            result.set(env, i, frame.get());
        }
        return result;
    }

    jint arrayLength(JNIEnv *env, jobject, const Array<FrameJ> &frames) { return frames.size(env); }

    Array<FrameJ> echoFrameArray(JNIEnv *, jobject, Array<FrameJ> frames) { return frames; }

    // java.util.List and java.util.Map to send a null that C++ types can not hold.
    struct ListJ : JavaClass<ListJ> {
        static constexpr const char *name = "java/util/List";
    };

    struct MapJ : JavaClass<MapJ> {
        static constexpr const char *name = "java/util/Map";
    };

    struct CollectorJ : JavaClass<CollectorJ> {
        static constexpr const char *name = "jnirt/test/Collector";

        static inline Constructor<CollectorJ, void()> create;

        // The natives, described with C++ types ...
        static inline Method<CollectorJ, Strings(Strings)> echoStrings{"echoStrings"};
        static inline Method<CollectorJ, OptStrings(OptStrings)> echoOptStrings{"echoOptStrings"};
        static inline Method<CollectorJ, std::vector<Frame>(std::vector<Frame>)> reversed{"reversed"};
        static inline Method<CollectorJ, std::vector<Strings>(std::vector<Strings>)> echoNested{"echoNested"};
        static inline Method<CollectorJ, std::vector<std::optional<jint> >(std::vector<std::optional<jint> >)>
        echoBoxed{"echoBoxed"};
        static inline Method<CollectorJ, std::vector<std::vector<jint> >(std::vector<std::vector<jint> >)> echoArrays{
            "echoArrays"
        };
        static inline Method<CollectorJ, jint(Strings)> totalLength{"totalLength"};
        static inline Method<CollectorJ, Strings(jint)> range{"range"};
        static inline Method<CollectorJ, std::vector<Probe>(std::vector<Probe>)> echoProbes{"echoProbes"};
        static inline Method<CollectorJ, Counts(Counts)> echoCounts{"echoCounts"};
        static inline Method<CollectorJ, std::unordered_map<std::string, Frame>(std::unordered_map<std::string, Frame>)>
        echoFrameMap{"echoFrameMap"};
        static inline Method<CollectorJ, std::map<jint, std::string>(std::map<jint, std::string>)> echoIntKeys{
            "echoIntKeys"
        };
        static inline Method<CollectorJ, std::map<std::string, std::optional<std::string> >(
            std::map<std::string, std::optional<std::string> >)> echoOptValues{"echoOptValues"};
        static inline Method<CollectorJ, Counts(jint)> numbered{"numbered"};
        static inline Method<CollectorJ, Frame(Frame)> echoFrame{"echoFrame"};
        static inline Method<CollectorJ, std::optional<Frame>(std::optional<Frame>)> maybeFrame{"maybeFrame"};
        static inline Method<CollectorJ, Array<FrameJ>(Array<FrameJ>)> firstTwo{"firstTwo"};
        static inline Method<CollectorJ, Array<FrameJ>(jint)> makeFrameArray{"makeFrameArray"};
        static inline Method<CollectorJ, jint(Array<FrameJ>)> arrayLength{"arrayLength"};
        static inline Method<CollectorJ, Array<FrameJ>(Array<FrameJ>)> echoFrameArray{"echoFrameArray"};

        // ... and with raw types, to send nulls.
        static inline Method<CollectorJ, ListJ(ListJ)> echoStringsRaw{"echoStrings"};
        static inline Method<CollectorJ, MapJ(MapJ)> echoCountsRaw{"echoCounts"};
        static inline Method<CollectorJ, Frame(std::optional<Frame>)> echoFrameOpt{"echoFrame"};

        // Java members that native code calls.
        static inline Field<CollectorJ, Strings> names{"names"};
        static inline Field<CollectorJ, Counts> ages{"ages"};
        static inline Field<CollectorJ, std::optional<Array<FrameJ> > > frames{"frames"};
        static inline Method<CollectorJ, Strings(jint)> makeList{"makeList"};
        static inline Method<CollectorJ, Strings(jint)> makeLinkedList{"makeLinkedList"};
        static inline Method<CollectorJ, Strings()> listWithNull{"listWithNull"};
        static inline Method<CollectorJ, OptStrings()> listWithNullOpt{"listWithNull"};
        static inline Method<CollectorJ, std::map<std::string, std::string>()> mapWithNullKey{"mapWithNullKey"};
        static inline Method<CollectorJ, std::map<std::optional<std::string>, std::string>()> mapWithNullKeyOpt{
            "mapWithNullKey"
        };
        static inline Method<CollectorJ, std::map<std::string, std::string>()> mapWithNullValue{"mapWithNullValue"};
        static inline Method<CollectorJ, std::map<std::string, std::optional<std::string> >()> mapWithNullValueOpt{
            "mapWithNullValue"
        };
        static inline Method<CollectorJ, jint(Strings)> sizeOf{"sizeOf"};
        static inline Method<CollectorJ, std::string(Strings)> joined{"joined"};
        static inline Method<CollectorJ, std::string(Counts)> describe{"describe"};
        static inline Method<CollectorJ, Strings()> nothingList{"nothingList"};
        static inline Method<CollectorJ, std::optional<Strings>()> nothingListOpt{"nothingList"};
        static inline Method<CollectorJ, Counts(jint)> manyEntries{"manyEntries"};
        static inline Method<CollectorJ, Strings(std::map<std::string, std::string, std::greater<> >)> keysInOrder{
            "keysInOrder"
        };
    };

    void registerCollector(Registry &r) {
        r.add<FrameJ>();
        r.add<CollectorJ>()
                .native<&echoStrings>("echoStrings")
                .native<&echoOptStrings>("echoOptStrings")
                .native<&reversed>("reversed")
                .native<&echoNested>("echoNested")
                .native<&echoBoxed>("echoBoxed")
                .native<&echoArrays>("echoArrays")
                .native<&totalLength>("totalLength")
                .native<&range>("range")
                .native<&echoProbes>("echoProbes")
                .native<&echoCounts>("echoCounts")
                .native<&echoFrameMap>("echoFrameMap")
                .native<&echoIntKeys>("echoIntKeys")
                .native<&echoOptValues>("echoOptValues")
                .native<&numbered>("numbered")
                .native<&echoFrame>("echoFrame")
                .native<&maybeFrame>("maybeFrame")
                .native<&firstTwo>("firstTwo")
                .native<&makeFrameArray>("makeFrameArray")
                .native<&arrayLength>("arrayLength")
                .native<&echoFrameArray>("echoFrameArray");
    }

    class ConvertCollectionTest : public ::testing::Test {
    protected:
        void SetUp() override {
            env = jnirt::env();
            Registry registry;
            registerCollector(registry);
            auto problems = detail::startup(env, registry,
                                            {&detail::classInfo<CollectorJ>(), &detail::classInfo<FrameJ>()});
            ASSERT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0]);
            self = CollectorJ::create(env);
            localRefsBefore = detail::liveLocalRefsOnThisThread();
            gPeakLocals = 0;
        }

        // Nothing leaked, nothing pending.
        void TearDown() override {
            EXPECT_FALSE(env->ExceptionCheck());
            EXPECT_EQ(detail::liveLocalRefsOnThisThread(), localRefsBefore) << "local references leaked";
        }

        jobject obj() const { return self.get(); }

        template<class F>
        std::string failureOf(F &&call) {
            try {
                call();
            } catch (const JavaException &e) {
                return e.what();
            } catch (const IllegalArgument &e) {
                // Java handed C++ a value that C++ can not hold: the C++ caller sees the contract violation
                // itself; only a native called from Java turns it into a Java exception.
                return std::string("IllegalArgument: ") + e.what();
            } catch (const IllegalState &e) {
                return std::string("IllegalState: ") + e.what();
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

    // ---- List ----

    TEST_F(ConvertCollectionTest, AListOfStringsGoesThereAndBack) {
        const Strings items{"one", "Привет", "камера 📷", "", std::string("a\0b", 3)};
        EXPECT_EQ(CollectorJ::echoStrings(env, obj(), items), items);
        EXPECT_EQ(CollectorJ::echoStrings(env, obj(), Strings{}), Strings{});
    }

    TEST_F(ConvertCollectionTest, AListWithNullsIsAListOfOptionals) {
        const OptStrings items{"a", std::nullopt, "c", std::nullopt};
        EXPECT_EQ(CollectorJ::echoOptStrings(env, obj(), items), items);
    }

    TEST_F(ConvertCollectionTest, ANullElementOfANonOptionalListIsAnErrorWithTheIndex) {
        std::string failure = failureOf([&] { CollectorJ::listWithNull(env, obj()); });
        EXPECT_TRUE(contains(failure, "IllegalArgument: element 1 is null")) << failure;
        EXPECT_TRUE(contains(failure, "Ljava/lang/String;")) << failure;
        EXPECT_EQ(CollectorJ::listWithNullOpt(env, obj()), (OptStrings{"a", std::nullopt}));
    }

    TEST_F(ConvertCollectionTest, ListsOfListsAndOfBoxedNumbersAndOfArrays) {
        const std::vector<Strings> nested{{"a", "b"}, {}, {"c"}};
        EXPECT_EQ(CollectorJ::echoNested(env, obj(), nested), nested);
        const std::vector<std::optional<jint> > boxed{1, std::nullopt, 3};
        EXPECT_EQ(CollectorJ::echoBoxed(env, obj(), boxed), boxed);
        const std::vector<std::vector<jint> > arrays{{1, 2}, {}, {3}};
        EXPECT_EQ(CollectorJ::echoArrays(env, obj(), arrays), arrays);
    }

    TEST_F(ConvertCollectionTest, ANullListIsAnErrorUnlessTheTypeIsOptional) {
        std::string failure = failureOf([&] { CollectorJ::echoStringsRaw(env, obj(), nullptr); });
        EXPECT_TRUE(contains(failure, "argument 1 is null")) << failure;
        EXPECT_TRUE(contains(failure, "Ljava/util/List;")) << failure;
        EXPECT_EQ(CollectorJ::nothingListOpt(env, obj()), std::nullopt);
        failure = failureOf([&] { CollectorJ::nothingList(env, obj()); });
        EXPECT_TRUE(contains(failure, "IllegalState: ")) << failure;
        EXPECT_TRUE(contains(failure, "returned null")) << failure;
    }

    TEST_F(ConvertCollectionTest, ALinkedListIsReadInOnePass) {
        Strings items = CollectorJ::makeLinkedList(env, obj(), 50000); // quadratic with get(i): minutes
        ASSERT_EQ(items.size(), 50000u);
        EXPECT_EQ(items.front(), "n0");
        EXPECT_EQ(items.back(), "n49999");
    }

    // ---- the references stay flat ----

    TEST_F(ConvertCollectionTest, ALongListNeedsAFewLocalReferencesAndNotOnePerElement) {
        Strings big = CollectorJ::makeList(env, obj(), 100000);
        ASSERT_EQ(big.size(), 100000u);
        EXPECT_EQ(big[99999], "item 99999");
        EXPECT_EQ(CollectorJ::sizeOf(env, obj(), big), 100000);
        Strings made = CollectorJ::range(env, obj(), 100000); // native: C++ vector -> List, and back through Java
        EXPECT_EQ(CollectorJ::totalLength(env, obj(), made), CollectorJ::totalLength(env, obj(), big));
    }

    TEST_F(ConvertCollectionTest, TheNumberOfLocalReferencesDoesNotDependOnTheLength) {
        // The counter exists in debug builds only; in release it is always 0 and this checks nothing more.
        std::vector<Probe> probes(100000, Probe{"x"});
        auto back = CollectorJ::echoProbes(env, obj(), probes); // written, converted back, written, read
        ASSERT_EQ(back.size(), probes.size());
        EXPECT_LE(gPeakLocals - localRefsBefore, 12) << "the peak must not grow with the number of elements";
#ifndef NDEBUG
        EXPECT_GT(gPeakLocals, localRefsBefore) << "the probe did see local references";
#endif
    }

    // ---- Map ----

    TEST_F(ConvertCollectionTest, AMapOfStringsToNumbersBoxesTheNumbers) {
        const Counts counts{{"a", 1}, {"b", 2}, {"", -5}};
        EXPECT_EQ(CollectorJ::echoCounts(env, obj(), counts), counts);
        EXPECT_EQ(CollectorJ::describe(env, obj(), counts), "{=-5, a=1, b=2}");
        EXPECT_EQ(CollectorJ::echoCounts(env, obj(), Counts{}), Counts{});
    }

    TEST_F(ConvertCollectionTest, AnUnorderedMapOfYourOwnType) {
        const std::unordered_map<std::string, Frame> frames{{"first", {1, "one"}}, {"second", {2, "два"}}};
        EXPECT_EQ(CollectorJ::echoFrameMap(env, obj(), frames), frames);
    }

    TEST_F(ConvertCollectionTest, NumbersAsKeys) {
        const std::map<jint, std::string> names{{1, "one"}, {-7, "minus seven"}};
        EXPECT_EQ(CollectorJ::echoIntKeys(env, obj(), names), names);
    }

    TEST_F(ConvertCollectionTest, ANullValueNeedsAnOptionalValue) {
        const std::map<std::string, std::optional<std::string> > map{{"a", "x"}, {"b", std::nullopt}};
        EXPECT_EQ(CollectorJ::echoOptValues(env, obj(), map), map);

        std::string failure = failureOf([&] { CollectorJ::mapWithNullValue(env, obj()); });
        EXPECT_TRUE(contains(failure, "IllegalArgument: the value of entry 0 is null")) << failure;
        EXPECT_EQ(CollectorJ::mapWithNullValueOpt(env, obj()).at("key"), std::nullopt);
    }

    TEST_F(ConvertCollectionTest, ANullKeyNeedsAnOptionalKey) {
        std::string failure = failureOf([&] { CollectorJ::mapWithNullKey(env, obj()); });
        EXPECT_TRUE(contains(failure, "the key of entry 0 is null")) << failure;
        auto withNull = CollectorJ::mapWithNullKeyOpt(env, obj());
        ASSERT_EQ(withNull.size(), 1u);
        EXPECT_EQ(withNull.begin()->first, std::nullopt);
        EXPECT_EQ(withNull.begin()->second, "value");
    }

    TEST_F(ConvertCollectionTest, JavaSeesTheIterationOrderOfTheCppMap) {
        const std::map<std::string, std::string, std::greater<> > backwards{{"a", "1"}, {"c", "3"}, {"b", "2"}};
        EXPECT_EQ(CollectorJ::keysInOrder(env, obj(), backwards), (Strings{"c", "b", "a"}));
    }

    TEST_F(ConvertCollectionTest, ABigMapKeepsLocalReferencesFlat) {
        Counts big = CollectorJ::manyEntries(env, obj(), 50000);
        ASSERT_EQ(big.size(), 50000u);
        EXPECT_EQ(big.at("k12345"), 12345);
        Counts made = CollectorJ::numbered(env, obj(), 30000);
        EXPECT_EQ(made.size(), 30000u);
        EXPECT_EQ(made.at("k29999"), 29999);
    }

    TEST_F(ConvertCollectionTest, ANullMapIsAnErrorUnlessTheTypeIsOptional) {
        std::string failure = failureOf([&] { CollectorJ::echoCountsRaw(env, obj(), nullptr); });
        EXPECT_TRUE(contains(failure, "argument 1 is null")) << failure;
        EXPECT_TRUE(contains(failure, "Ljava/util/Map;")) << failure;
    }

    // ---- your own type ----

    TEST_F(ConvertCollectionTest, YourOwnTypeCrossesAsAnObject) {
        const Frame frame{7, "seven"};
        EXPECT_EQ(CollectorJ::echoFrame(env, obj(), frame), frame);
        EXPECT_EQ(CollectorJ::maybeFrame(env, obj(), frame), std::optional<Frame>(frame));
        EXPECT_EQ(CollectorJ::maybeFrame(env, obj(), std::nullopt), std::nullopt);
        const std::vector<Frame> frames{{1, "a"}, {2, "b"}, {3, "c"}};
        EXPECT_EQ(CollectorJ::reversed(env, obj(), frames), (std::vector<Frame>{{3, "c"}, {2, "b"}, {1, "a"}}));
    }

    TEST_F(ConvertCollectionTest, ANullFrameIsAnErrorUnlessItIsOptional) {
        std::string failure = failureOf([&] { CollectorJ::echoFrameOpt(env, obj(), std::nullopt); });
        EXPECT_TRUE(contains(failure, "argument 1 is null")) << failure;
        EXPECT_TRUE(contains(failure, "Ljnirt/test/Frame;")) << failure;
    }

    // ---- Array<C> ----

    TEST_F(ConvertCollectionTest, AnArrayOfObjectsIsAViewOfTheJavaArray) {
        Array<FrameJ> frames = CollectorJ::makeFrameArray(env, obj(), 3);
        ASSERT_TRUE(frames);
        EXPECT_EQ(frames.size(env), 3);
        EXPECT_EQ(CollectorJ::arrayLength(env, obj(), frames), 3);
        LocalRef<jobject> second = frames.get(env, 1);
        EXPECT_EQ(jnirt::fromJava<Frame>(env, second.get()), (Frame{1, "frame 1"}));

        Array<FrameJ> two = CollectorJ::firstTwo(env, obj(), frames);
        EXPECT_EQ(two.size(env), 2);
        EXPECT_EQ(jnirt::fromJava<Frame>(env, two.get(env, 0).get()), (Frame{0, "frame 0"}));
        Array<FrameJ> same = CollectorJ::echoFrameArray(env, obj(), frames);
        EXPECT_EQ(same.size(env), 3);
    }

    TEST_F(ConvertCollectionTest, NullElementsAndBadIndexes) {
        Array<FrameJ> frames = newArray<FrameJ>(env, 2);
        EXPECT_FALSE(frames.get(env, 0));
        EXPECT_THROW(frames.get(env, 5), JavaException);
        LocalRef<jobject> frame = FrameJ::create(env, 1, "x");
        frames.set(env, 1, frame.get());
        EXPECT_TRUE(frames.get(env, 1));
        EXPECT_THROW(frames.set(env, 2, frame.get()), JavaException);
        EXPECT_FALSE(env->ExceptionCheck());
    }

    TEST_F(ConvertCollectionTest, AnArrayInAFieldMayBeNull) {
        auto frames = CollectorJ::frames.get(env, obj());
        ASSERT_TRUE(frames.has_value());
        EXPECT_EQ(frames->size(env), 2);
        EXPECT_TRUE(frames->get(env, 0));
        EXPECT_FALSE(frames->get(env, 1));
        CollectorJ::frames.set(env, obj(), std::nullopt);
        EXPECT_EQ(CollectorJ::frames.get(env, obj()), std::nullopt);
    }

    TEST_F(ConvertCollectionTest, ANullArrayIsAnErrorWhereTheTypeHasNoNull) {
        std::string failure = failureOf([&] { CollectorJ::arrayLength(env, obj(), Array<FrameJ>()); });
        EXPECT_TRUE(contains(failure, "argument 1 is null")) << failure;
        EXPECT_TRUE(contains(failure, "[Ljnirt/test/Frame;")) << failure;
    }

    // ---- fields ----

    TEST_F(ConvertCollectionTest, FieldsHoldListsAndMaps) {
        EXPECT_EQ(CollectorJ::names.get(env, obj()), (Strings{"a", "b"}));
        CollectorJ::names.set(env, obj(), Strings{"x", "y", "z"});
        EXPECT_EQ(CollectorJ::names.get(env, obj()), (Strings{"x", "y", "z"}));
        EXPECT_EQ(CollectorJ::ages.get(env, obj()), (Counts{{"x", 1}}));
        CollectorJ::ages.set(env, obj(), Counts{{"q", 9}});
        EXPECT_EQ(CollectorJ::ages.get(env, obj()), (Counts{{"q", 9}}));
        EXPECT_EQ(CollectorJ::joined(env, obj(), Strings{"p", "q"}), "p,q");
    }
} // namespace

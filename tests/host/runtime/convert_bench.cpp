// The cost of crossing, printed (ADR-0008 §10). Not a test: it is not registered with ctest and has no thresholds.
//
//   cmake --build build --target jnirt_convert_bench && build/tests/host/runtime/jnirt_convert_bench
//
// Java calls natives that convert their parameters, and the loop is timed on the Java side (Bench.java), the way a
// Kotlin app calls native code. Build it in Release (-DCMAKE_BUILD_TYPE=Release): a Debug build counts every local
// reference. The same natives, driven from Kotlin, are in examples/p5_bench for a real device.

#include <jnirt/runtime/convert.h>
#include <jnirt/runtime/registry.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

using namespace jnirt;

namespace {
    struct BenchJ : JavaClass<BenchJ> {
        static constexpr const char *name = "jnirt/test/Bench";

        static inline StaticMethod<BenchJ, std::string()> run{"run"};
    };

    // baseline: raw JNI, nothing converted
    jstring echoRaw(JNIEnv *, jobject, jstring text) { return text; }

    std::string echoString(JNIEnv *, jobject, const std::string &text) { return text; }

    jint plainInt(JNIEnv *, jobject, jint value) { return value; }

    std::optional<jint> boxedInt(JNIEnv *, jobject, std::optional<jint> value) { return value; }

    std::vector<jint> echoInts(JNIEnv *, jobject, std::vector<jint> values) { return values; }

    jlong sumCopy(JNIEnv *, jobject, const std::vector<jint> &values) {
        return std::accumulate(values.begin(), values.end(), jlong{0});
    }

    jlong sumPieces(JNIEnv *env, jobject, jintArray values) {
        constexpr jsize kPiece = 4096;
        jint piece[kPiece];
        jlong total = 0;
        const jsize length = arrayLength(env, values);
        for (jsize start = 0; start < length; start += kPiece) {
            const jsize count = std::min(kPiece, length - start);
            readArray(env, values, start, piece, count);
            total += std::accumulate(piece, piece + count, jlong{0});
        }
        return total;
    }

    jlong sumCritical(JNIEnv *env, jobject, jintArray values) {
        return withCriticalRead(env, values, [](const jint *data, std::size_t size) {
            return std::accumulate(data, data + size, jlong{0});
        });
    }

    jlong sumDirect(JNIEnv *, jobject, DirectBuffer buffer) {
        const auto *bytes = buffer.as<std::uint8_t>();
        return std::accumulate(bytes, bytes + buffer.size(), jlong{0});
    }

    std::vector<std::string> echoList(JNIEnv *, jobject, const std::vector<std::string> &items) { return items; }

    std::map<std::string, jint> echoMap(JNIEnv *, jobject, const std::map<std::string, jint> &counts) {
        return counts;
    }

    void registerBench(Registry &r) {
        r.add<BenchJ>()
                .native<&echoRaw>("echoRaw")
                .native<&echoString>("echoString")
                .native<&plainInt>("plainInt")
                .native<&boxedInt>("boxedInt")
                .native<&echoInts>("echoInts")
                .native<&sumCopy>("sumCopy")
                .native<&sumPieces>("sumPieces")
                .native<&sumCritical>("sumCritical")
                .native<&sumDirect>("sumDirect")
                .native<&echoList>("echoList")
                .native<&echoMap>("echoMap");
    }
} // namespace

int main() {
    std::string classPath = std::string("-Djava.class.path=") + JNIRT_TEST_CLASSPATH;
    JavaVMOption options[] = {{const_cast<char *>(classPath.c_str()), nullptr}};
    JavaVMInitArgs args{JNI_VERSION_1_6, 1, options, JNI_FALSE};
    JavaVM *vm = nullptr;
    JNIEnv *env = nullptr;
    if (JNI_CreateJavaVM(&vm, reinterpret_cast<void **>(&env), &args) != JNI_OK) {
        std::fprintf(stderr, "could not start a JVM\n");
        return 1;
    }
    if (init(vm, {&registerBench}) == JNI_ERR) {
        std::fprintf(stderr, "jnirt::init failed\n");
        return 1;
    }
    std::string report = BenchJ::run(env);
    std::fputs(report.c_str(), stdout);
    return 0;
}

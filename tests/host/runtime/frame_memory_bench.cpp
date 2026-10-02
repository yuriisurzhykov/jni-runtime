// How much native memory is alive when frames are made and dropped (ADR-0010 §9). Not a test: not in ctest.
//
//   cmake --build build --target jnirt_frame_memory_bench
//   JAVA_TOOL_OPTIONS=-Xmx64m build/tests/host/runtime/jnirt_frame_memory_bench
//   JNIRT_BENCH_NO_NUDGE=1 ...    the same without the collection that jnirt asks for
//
// A direct ByteBuffer made by NewDirectByteBuffer does not tell the garbage collector how big its memory is, so the
// memory of dropped frames waits for a collection that the Java heap does not ask for. This prints the peak.

#include <jnirt/runtime/native_buffer.h>
#include <jnirt/runtime/registry.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace jnirt;

namespace {
    std::atomic<long long> gLive{0};

    NativeBuffer frame(JNIEnv *, jclass, jint size) {
        std::shared_ptr<std::byte> memory(new std::byte[static_cast<std::size_t>(size)](), [size](std::byte *p) {
            gLive -= size;
            delete[] p;
        });
        gLive += size;
        std::byte *data = memory.get();
        return NativeBuffer(std::move(memory), data, static_cast<std::size_t>(size));
    }

    jlong liveBytes(JNIEnv *, jclass) { return gLive; }

    jlong forcedCollections(JNIEnv *, jclass) { return static_cast<jlong>(detail::forcedCollections()); }

    struct ByteBufferJ : JavaClass<ByteBufferJ> {
        static constexpr const char *name = "java/nio/ByteBuffer";
    };

    struct FrameMemoryJ : JavaClass<FrameMemoryJ> {
        static constexpr const char *name = "jnirt/test/FrameMemory";

        static inline StaticMethod<FrameMemoryJ, std::string(jint, jint, jint)> run{"run"};
    };

    void registerBench(Registry &r) {
        r.add<FrameMemoryJ>().native<&frame>("frame").native<&liveBytes>("liveBytes").native<&forcedCollections>(
            "forcedCollections");
        r.add<ByteBufferJ>();
    }
} // namespace

int main() {
    std::string classPath = std::string("-Djava.class.path=") + JNIRT_TEST_CLASSPATH;
    JavaVMOption options[] = {{const_cast<char *>(classPath.c_str()), nullptr}};
    JavaVMInitArgs args{JNI_VERSION_1_6, 1, options, JNI_FALSE};
    JavaVM *vm = nullptr;
    JNIEnv *env = nullptr;
    if (JNI_CreateJavaVM(&vm, reinterpret_cast<void **>(&env), &args) != JNI_OK) return 1;
    if (init(vm, {&registerBench}) == JNI_ERR) return 1;
    if (std::getenv("JNIRT_BENCH_NO_NUDGE") != nullptr) detail::setBufferGcThreshold(static_cast<std::size_t>(-1));
    std::fputs(FrameMemoryJ::run(env, 3000, 1024 * 1024, 2000).c_str(), stdout);
    return 0;
}

// Playground for jnirt::env() and the three reference types. Not part of the library: read it, run it, break it.
//
//   cmake -S . -B build -DJNIRT_BUILD_EXAMPLES=ON && cmake --build build --target refs_playground
//   ./build/examples/refs_playground
//
// Each step is a function with one idea. Every "TRY:" comment is an experiment: uncomment it, rebuild, read what
// happens. Start with the output of the unchanged program, then do the experiments one by one.

#include <jnirt/runtime/env.h>
#include <jnirt/runtime/ref.h>
#include <jnirt/runtime/vm.h>

#include <pthread.h>

#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {
    // Starts a JVM inside this process. On Android this is not needed: the JVM already exists and gives us the
    // JavaVM in JNI_OnLoad. Here the main thread plays "a Java thread" (the JVM attaches it for us).
    JNIEnv *startJvm() {
        JavaVM *vm = nullptr;
        JNIEnv *env = nullptr;
        JavaVMOption options[] = {{const_cast<char *>("-Xcheck:jni"), nullptr}};
        JavaVMInitArgs args{JNI_VERSION_1_6, 1, options, JNI_FALSE};
        if (JNI_CreateJavaVM(&vm, reinterpret_cast<void **>(&env), &args) != JNI_OK) {
            std::fprintf(stderr, "could not start a JVM\n");
            std::exit(1);
        }
        jnirt::setVm(vm); // what jnirt::init(vm, ...) will do in JNI_OnLoad
        return env;
    }

    // After every call into Java: -Xcheck:jni requires an ExceptionCheck before the next JNI call (ADR-0001 5.5).
    // The typed calls of class_cache (P3.5) will do this for you; until then it is a line you write by hand.
    void check(JNIEnv *env) {
        if (!env->ExceptionCheck()) return;
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    // Small helper so the steps below stay short: "Thread.currentThread().getName()" for the calling thread.
    // Everything here is wrapped in LocalRef: nothing to delete by hand.
    void printJavaThreadName(const char *label) {
        JNIEnv *env = jnirt::env();
        jnirt::LocalRef<jclass> threadClass(env, env->FindClass("java/lang/Thread"));
        jmethodID current = env->GetStaticMethodID(threadClass.get(), "currentThread", "()Ljava/lang/Thread;");
        jnirt::LocalRef<jobject> thread(env, env->CallStaticObjectMethod(threadClass.get(), current));
        check(env);
        jmethodID getName = env->GetMethodID(threadClass.get(), "getName", "()Ljava/lang/String;");
        jnirt::LocalRef<jstring> name(env, static_cast<jstring>(env->CallObjectMethod(thread.get(), getName)));
        check(env);
        const char *chars = env->GetStringUTFChars(name.get(), nullptr);
        std::printf("  %-28s Java sees this thread as \"%s\"\n", label, chars);
        env->ReleaseStringUTFChars(name.get(), chars);
    }

    // 1. LocalRef: deleted at the end of its scope. Ten thousand iterations would overflow the local reference
    //    table without it (about 512 entries on ART).
    void step1_localRefInALoop(JNIEnv *env) {
        std::printf("1. LocalRef in a loop\n");
        for (int i = 0; i < 10000; ++i) {
            jnirt::LocalRef<jstring> text(env, env->NewStringUTF("frame"));
            // TRY: replace the line above with `jstring text = env->NewStringUTF("frame");` and run again.
            //      -Xcheck:jni warns about the leaked local references.
        }
        std::printf("  10000 strings created and deleted\n");
    }

    // 2. env() on a thread the JVM does not know. jnirt attaches it under its pthread name and detaches it when
    //    the thread exits. Nobody writes AttachCurrentThread / DetachCurrentThread.
    void step2_nativeThread() {
        std::printf("2. A native thread\n");
        std::thread([] {
            pthread_setname_np(pthread_self(), "camera-rx");
            printJavaThreadName("native thread, first env():");
            // TRY: remove the pthread_setname_np line: the thread shows up as "Thread-N" or empty.
        }).join();
    }

    // 3. GlobalRef: valid after the local one is gone, usable on another thread.
    void step3_globalRefCrossesThreads(JNIEnv *env) {
        std::printf("3. GlobalRef across threads\n");
        jnirt::GlobalRef<jstring> global;
        {
            jnirt::LocalRef<jstring> local(env, env->NewStringUTF("created on main"));
            global = jnirt::GlobalRef<jstring>(local.get());
        } // `local` is deleted here, `global` is not

        std::thread([moved = std::move(global)]() mutable {
            // `moved` is a GlobalRef, so it is fine to use it here. The thread's own env() comes from jnirt.
            JNIEnv *workerEnv = jnirt::env();
            std::printf("  worker reads a string of length %d\n", workerEnv->GetStringLength(moved.get()));
            // TRY: make the lambda capture a LocalRef instead (keep `local` alive outside and move it in).
            //      The destructor then runs on the wrong thread and jnirt aborts with a message.
        }).join();
    }

    // 4. WeakGlobalRef: does not keep the object alive. promote() gives a LocalRef or nothing.
    void step4_weakRef(JNIEnv *env) {
        std::printf("4. WeakGlobalRef\n");
        jnirt::LocalRef<jclass> objectClass(env, env->FindClass("java/lang/Object"));
        jmethodID init = env->GetMethodID(objectClass.get(), "<init>", "()V");

        jnirt::LocalRef<jobject> strong(env, env->NewObject(objectClass.get(), init));
        check(env);
        jnirt::WeakGlobalRef<jobject> weak(strong.get());
        std::printf("  while `strong` is alive: promote() %s\n", weak.promote(env) ? "returns the object" : "is empty");

        strong.reset(); // the only strong reference is gone
        jnirt::LocalRef<jclass> systemClass(env, env->FindClass("java/lang/System"));
        jmethodID gc = env->GetStaticMethodID(systemClass.get(), "gc", "()V");
        for (int i = 0; i < 20 && weak.promote(env); ++i) { env->CallStaticVoidMethod(systemClass.get(), gc); check(env); }
        std::printf("  after System.gc():          promote() %s\n", weak.promote(env) ? "still returns it" : "is empty");
        // TRY: before strong.reset() add `jnirt::GlobalRef<jobject> keep(strong.get());`. The object now has a
        //      strong owner, and promote() keeps returning it after the gc loop.
    }

    // 5. The mistake jnirt catches for you. Uncomment the body and run: the process aborts with a three-line
    //    message (what happened, why it is bad, how to fix it).
    void step5_detachWithLiveLocalRef() {
        std::printf("5. Mistakes are reported (uncomment to see)\n");
        // std::thread([] {
        //     JNIEnv *env = jnirt::env();
        //     jnirt::LocalRef<jstring> text(env, env->NewStringUTF("still alive"));
        //     jnirt::detachCurrentThread();       // `text` is still in scope: jnirt aborts here
        //     // FIX: put the LocalRef in an inner { } block that ends before detachCurrentThread().
        // }).join();
    }
} // namespace

int main() {
    JNIEnv *env = startJvm();
    step1_localRefInALoop(env);
    step2_nativeThread();
    step3_globalRefCrossesThreads(env);
    step4_weakRef(env);
    step5_detachWithLiveLocalRef();
    std::printf("done\n");
    return 0; // the JVM is not destroyed on purpose, see tests/host/runtime/env_test.cpp
}

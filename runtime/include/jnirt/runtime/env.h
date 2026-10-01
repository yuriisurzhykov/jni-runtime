// JNIEnv for the current thread, on any thread. See docs/adr/0003-vm-and-env.md and
// docs/learning/vm-env-by-example.md.
//
//   JNIEnv* env = jnirt::env();      // Java thread: its env. Native thread: attached on first call.
//   ...
//   jnirt::detachCurrentThread();    // optional, last statement of a thread function
//
// A thread attached by jnirt is detached automatically when it exits. A thread attached by anyone else (a Java
// thread, other native code) is never detached by jnirt.
//
// If a function receives JNIEnv* as a parameter, use that one. Never store JNIEnv* or pass it to another thread.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNIRT_RUNTIME_ENV_H
#define JNIRT_RUNTIME_ENV_H

#pragma once
#include <jni.h>

namespace jnirt {
    // The JNIEnv of the current thread, never nullptr. A thread unknown to the JVM is attached, under its
    // pthread name, and detached by jnirt when it exits. Crashes if the thread can not be attached.
    JNIEnv *env();

    // Detaches the current thread now instead of at thread exit. Call it as the last statement of the thread
    // function, after every local reference is gone. A later env() on the same thread attaches it again.
    //   attached by jnirt        -> detached
    //   not attached             -> nothing happens
    //   attached by someone else -> throws jnirt::IllegalState (IllegalStateException in Kotlin)
    void detachCurrentThread();
} // namespace jnirt

#endif //JNIRT_RUNTIME_ENV_H

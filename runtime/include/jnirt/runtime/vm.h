// The process-wide JavaVM. See docs/adr/0003-vm-and-env.md.
//
// Only JavaVM* is cached (ADR-0001 3.1). It is set once from JNI_OnLoad and never reset: on Android the library
// is never unloaded, so there is no teardown.
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNIRT_RUNTIME_VM_H
#define JNIRT_RUNTIME_VM_H

#pragma once
#include <jni.h>

namespace jnirt {
    // Stores the JavaVM. Called by jnirt::init() from JNI_OnLoad.
    // The same vm again is fine (several .so in one process); a different one is a crash.
    void setVm(JavaVM *vm);

    // The JavaVM stored by setVm(). Crashes with a clear message if setVm() was never called.
    JavaVM *vm();
} // namespace jnirt

#endif //JNIRT_RUNTIME_VM_H

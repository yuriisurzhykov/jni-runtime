# The Proguard/R8 rules for consumers of the JNI Runtime library

# Provide any rules below.

# jnirt::init reads NativeLoader.VERSION and calls NativeLoader.nativeMethodNames from native code (ADR-0006).
-keep class jnirt.NativeLoader { *; }

# The Proguard/R8 rules for consumers of the JNI Runtime library

# Provide any rules below.

# jnirt::init reads NativeLoader.VERSION and calls NativeLoader.nativeMethodNames from native code (ADR-0006).
-keep class jnirt.NativeLoader { *; }

# jnirt::init finds these by name, and entry() (ADR-0007) creates NativeException and calls NativeLogBridge.log from
# native code. R8 sees no Kotlin caller for them.
-keep class jnirt.NativeException { <init>(...); }
-keep class jnirt.NativeLogBridge { *; }

# jnirt::init registers NativePeerNatives.close itself (ADR-0009), and NativePeer.close() calls it.
-keep class jnirt.NativePeerNatives { *; }

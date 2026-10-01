package jnirt.test;

// What Kotlin makes of some shapes, written by hand (no Kotlin compiler in the host tests): a companion object, an
// `internal` function (name$module), default parameter values (name$default). The kotlin_hints tests look for methods
// that are not where the C++ side expects them.
public class KotlinShapes {
    // object Companion { fun count(x: Int) = x }   without @JvmStatic
    public static class Companion {
        public int count(int x) {
            return x;
        }
    }

    public static final Companion Companion = new Companion();

    // internal fun open(x: Int)
    public int open$module(int x) {
        return x;
    }

    // fun close(a: Int, b: Int = 0): the full method, and the one Kotlin calls when a default is used
    public int close(int a, int b) {
        return a + b;
    }

    public int close$default(int a, int b, int mask, Object marker) {
        return a;
    }

    // internal external fun ping(x: Int)
    public native int ping$module(int x);
}

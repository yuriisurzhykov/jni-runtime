package jnirt.test;

import jnirt.NativeException;

// The class of the entry() tests. Its natives throw C++ exceptions of every kind; probe() turns whatever arrives in
// Java into one line, so the tests compare text.
public class Thrower {
    public Throwable expected = new IllegalStateException("original");

    public native void boom(int kind);

    public native int add(int a, int b);

    public static native int staticBoom(int kind);

    public native String greeting(int kind);

    public native void callFailing();

    public native void rawPending();

    // called from native: throws the same object every time
    public void failing() throws Throwable {
        throw expected;
    }

    public boolean identity() {
        try {
            callFailing();
            return false;
        } catch (Throwable t) {
            return t == expected;
        }
    }

    public static String probe(int kind) {
        try {
            new Thrower().boom(kind);
            return "no exception";
        } catch (Throwable t) {
            return describe(t);
        }
    }

    public static String probeStatic(int kind) {
        try {
            staticBoom(kind);
            return "no exception";
        } catch (Throwable t) {
            return describe(t);
        }
    }

    public static String probeObject(int kind) {
        try {
            String text = new Thrower().greeting(kind);
            return text == null ? "null, no exception" : text;
        } catch (Throwable t) {
            return describe(t);
        }
    }

    public static String probeRawPending() {
        try {
            new Thrower().rawPending();
            return "no exception";
        } catch (Throwable t) {
            return describe(t);
        }
    }

    // the first two frames of the stack trace of what arrives: the native method, then its Java caller
    public static String probeCaller(int kind) {
        try {
            new Thrower().boom(kind);
            return "no exception";
        } catch (Throwable t) {
            StackTraceElement[] stack = t.getStackTrace();
            return stack[0] + " | " + stack[1];
        }
    }

    public static int depth(int kind) {
        try {
            new Thrower().boom(kind);
            return 0;
        } catch (Throwable t) {
            int count = 0;
            for (Throwable c = t; c != null; c = c.getCause()) count++;
            return count;
        }
    }

    // "jnirt.NativeException[domain/code]: message <- java.lang.IllegalStateException: original"
    public static String describe(Throwable t) {
        StringBuilder text = new StringBuilder();
        for (Throwable c = t; c != null; c = c.getCause()) {
            if (text.length() > 0) text.append(" <- ");
            text.append(c.getClass().getName());
            if (c instanceof NativeException) {
                NativeException n = (NativeException) c;
                text.append('[').append(n.domain).append('/').append(n.code).append(']');
            }
            text.append(": ").append(c.getMessage());
        }
        return text.toString();
    }
}

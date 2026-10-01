package jnirt.test;

// The class the class_cache and registry tests describe. The members with `native` are registered by the tests.
public class Camera implements Listener {
    public long handle;
    public String label;
    public Listener listener;
    public static int counter;
    public static String title = "camera";

    public int frames;
    public String lastName;

    public Camera() {
    }

    public Camera(String label) {
        if (label == null) throw new IllegalArgumentException("label is null");
        this.label = label;
    }

    public int add(int a, int b) {
        return a + b;
    }

    public String greet(String name) {
        return "hello " + name;
    }

    public Camera self() {
        return this;
    }

    public void fail(String message) {
        throw new IllegalStateException(message);
    }

    public void onFrame(int index, String name) {
        frames += index;
        lastName = name;
    }

    public static int twice(int x) {
        return 2 * x;
    }

    public static void failStatic() {
        throw new UnsupportedOperationException("static boom");
    }

    // registered by the tests
    public native boolean nativeOpen(String url);

    public static native int nativeStatic(int x);

    public native void nativeSubscribe(Listener listener);

    // declared native, never registered
    public native void forgotten();

    // a native that Kotlin declares static, while the C++ function may take jobject (and the other way round)
    public static native void staticOnly();

    public native void instanceOnly();

    // not native at all
    public void plain() {
    }
}
